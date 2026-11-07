package main

// alert: the leak alert, the Wi-Fi change through the server and the 5-min wake mode (DESIGN.md 5.3.13)

import (
	"encoding/json"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strconv"
	"strings"
	"time"
)

var (
	mediaDir = Media
	mediaRe  = regexp.MustCompile(`^[0-9A-F]{12}-[0-9]{10}-[pv][0-9]{1,2}$`)
)

const (
	Media       = "/home/pi/leakcam-server/media"       // LEAKCAM_MEDIA: the boards' pictures and videos, by ID
	LeakProbeMV = 1100                                  // a probe below this is wet: the firmware's comparator reference (leak_check.c VIO_LEAK)
	WakeModeS   = 300                                   // the check-in interval in the wake mode after a failed Wi-Fi change
	LeakModeS   = 60                                    // during a leak: near real time (user 2026-11-07), ~60 mAh a day
	WakeModeMax = 24 * time.Hour                        // the mode ends after this at the latest: it costs the battery ~13 mAh a day
	Tokens      = "/home/pi/leakcam-server/tokens.json" // LEAKCAM_TOKENS: per phone its FCM token
	WifiSaved   = 0x04                                  // wifi_check's state for a network joined, checked and stored
	VideoS      = 15                                    // a video's length (user, 2026-11-07; DESIGN.md 5.3.14)
	MediaList   = 20                                    // the newest pictures and videos listed per device
	MediaMax    = 32 << 20                              // the largest upload (a 15 s video of both cameras)
	LeakPhotos  = 4                                     // every leak is recorded: 4 pictures and one 15 s video (user, 2026-11-07)
)

// Leak: a device's open leak, until a phone's all-clear
type Leak struct {
	Since time.Time `json:"since"`
	Why   string    `json:"why"`
}

// WifiChange: a new network for a device, handed over at its next check-in (one: a new ask replaces it); the
// passphrase forgotten once delivered
type WifiChange struct {
	SSID      string
	PSK       string
	Asked     time.Time
	Delivered time.Time
	Result    int // the board's wifi_check state for it (wchg), 0 = none yet
}

// Capture: a photo or video asked for (or a leak's record), handed over at the device's next check-in
type Capture struct {
	Kind  string
	Asked time.Time
}

// Pending: one thing waiting for a device's check-in, as the app lists it (user 2026-11-07: a list where the asks add
// up, server driven)
type Pending struct {
	Kind  string `json:"kind"`           // "photo", "video", "leak" (a leak's record), "wifi"
	SSID  string `json:"ssid,omitempty"` // wifi: the new network
	Asked int64  `json:"asked"`          // unix s
	State string `json:"state"`          // "waiting": for its next check-in; "sent": handed over, its result awaited
}

var (
	leaks       = map[string]Leak{}
	wifiChanges = map[string]*WifiChange{}
	tokens      = map[string]string{} // phone -> FCM token
	// per device the photos and videos asked for, one of each kind, in the order they are handed over (Rank): one
	// per check-in (the board takes one at a time)
	captures = map[string][]Capture{}
	// per device what the person called not a leak (user 2026-11-07): a probe still wet stays quiet until it has
	// dried; the camera inference takes a new reference picture (`rearm` in the next check-in's answer)
	notLeak = map[string]string{}
	// what a check-in brings first (user 2026-11-07): a leak's record (the emergency), a Wi-Fi change, a photo, a video
	Rank      = map[string]int{"leak": 0, "wifi": 1, "photo": 2, "video": 3}
	tokenFile = Tokens
)

// leakIn: why this check-in shows a leak ("" none): a wet probe, or the leak job's camera change in this check-in
func leakIn(m map[string]any) string {
	if p, ok := m["probe"].(int64); ok && p >= 0 && p < LeakProbeMV {
		return "probe wet"
	}
	if j, ok := m["job"].(string); ok && strings.Contains(j, "changed") {
		if ago, ok := m["jago"].(int64); ok && ago >= 0 && ago <= 60 {
			return "camera change"
		}
	}
	return ""
}

// checkInAlerts: a check-in's leak and Wi-Fi change result, and its answer's part (mode, a pending network); the
// messages to send once mu is released; mu held
func checkInAlerts(id string, in CheckIn, ans map[string]any) (msgs []map[string]string) {
	m := in.Metrics
	why := leakIn(m)
	switch notLeak[id] {
	case "probe wet": // re-armed once the probe is dry: the next wet probe is a leak again
		if why == "probe wet" {
			why = ""
		} else {
			delete(notLeak, id)
		}
	case "camera change":
		ans["rearm"] = 1
		delete(notLeak, id)
		if why == "camera change" {
			why = ""
		}
	}
	if why != "" {
		if _, open := leaks[id]; !open {
			leaks[id] = Leak{Since: time.Now(), Why: why}
			log.Printf("%s: leak (%s)", id, why)
			// the record of every leak, should the board not have started it itself: before anything asked
			queueCapture(id, "leak")
			// what the alert shows: which LEAKCAM (its ID, its network), what found it, when
			rssi := ""
			if v, ok := m["rssi"].(int64); ok {
				rssi = strconv.FormatInt(v, 10)
			}
			msgs = append(msgs, map[string]string{"type": "leak", "id": id, "why": why, "ssid": in.SSID,
				"rssi": rssi, "at": strconv.FormatInt(in.At.Unix(), 10),
				// the new pictures and video coming with it, each by its ID: the app shows one tile per ID, a spinner
				// until it is downloaded (user, 2026-11-07); the board uploads them under the same IDs
				"media": strings.Join(mediaIDs(id, "leak", in.At), ",")})
		}
	}
	if c := wifiChanges[id]; c != nil && !c.Delivered.IsZero() && c.Result == 0 {
		if st, ok := m["wchg"].(int64); ok && st > 0 {
			c.Result = int(st)
			log.Printf("%s: Wi-Fi change to %q: state 0x%02x", id, c.SSID, st)
			if st != WifiSaved {
				msgs = append(msgs, map[string]string{"type": "wifi_failed", "id": id, "ssid": c.SSID})
			}
		}
	}
	// one thing per check-in, by Rank: a leak's record, else a waiting network, else a photo, else a video (the board
	// checks in again right after a Wi-Fi change: the next one follows at once)
	q := captures[id]
	if c := wifiChanges[id]; c != nil && c.Delivered.IsZero() && (len(q) == 0 || q[0].Kind != "leak") {
		ans["wifi"] = map[string]string{"ssid": c.SSID, "psk": c.PSK}
		c.Delivered, c.PSK = time.Now(), ""
	} else if len(q) > 0 {
		k := q[0].Kind
		c := map[string]any{"kind": k, "ids": mediaIDs(id, k, in.At)}
		switch k {
		case "video":
			c["secs"] = VideoS
		case "leak":
			c["photos"], c["secs"] = LeakPhotos, VideoS
		}
		ans["capture"] = c
		if captures[id] = q[1:]; len(captures[id]) == 0 {
			delete(captures, id)
		}
		log.Printf("%s: %s handed over, %d more waiting", id, k, len(q)-1)
	}
	if s := wakeModeS(id); s > 0 {
		ans["checkin"] = s
		ans["mode"] = 1
	} else {
		ans["mode"] = 0
	}
	return msgs
}

// wakeModeS: the check-in interval while the device stays reachable (BLE sleep between check-ins), 0 when it is not:
// an open leak every minute (a request reaches it within a minute, user 2026-11-07), a failed Wi-Fi change every 5
// min; for at most WakeModeMax, a leak until the person's Not a leak or all clear too; mu held
func wakeModeS(id string) int {
	if l, open := leaks[id]; open && time.Since(l.Since) < WakeModeMax {
		return LeakModeS
	}
	if c := wifiChanges[id]; c != nil && c.Result != 0 && c.Result != WifiSaved && time.Since(c.Delivered) < WakeModeMax {
		return WakeModeS
	}
	return 0
}

// notify: each message to every phone of its device that told its FCM token; failures logged (the next check-in
// does not repeat a leak: the phone's app also shows it from GET /v1/devices/{id})
func notify(msgs []map[string]string) {
	for _, msg := range msgs {
		mu.Lock()
		var ts []string
		for _, p := range phones[msg["id"]] {
			if t := tokens[p]; t != "" {
				ts = append(ts, t)
			}
		}
		mu.Unlock()
		if len(ts) == 0 {
			log.Printf("%s: %s: no phone with an FCM token", msg["id"], msg["type"])
		}
		for _, t := range ts {
			if err := fcmSend(t, msg); err != nil {
				log.Printf("%s: %s: %v", msg["id"], msg["type"], err)
			} else {
				log.Printf("%s: %s pushed", msg["id"], msg["type"])
			}
		}
	}
}

// putToken: PUT /v1/phones/{phone}/token {"token":"..."}: the app's FCM token (at every start and on a new one)
func putToken(w http.ResponseWriter, r *http.Request) {
	phone := r.PathValue("phone")
	var b struct {
		Token string `json:"token"`
	}
	if !phoneRe.MatchString(phone) || json.NewDecoder(http.MaxBytesReader(w, r.Body, 1024)).Decode(&b) != nil ||
		b.Token == "" || len(b.Token) > 512 {
		http.Error(w, "phone: 16 hex digits; body {\"token\": <FCM token>}", http.StatusBadRequest)
		return
	}
	mu.Lock()
	tokens[phone] = b.Token
	err := saveJSON(tokenFile, tokens)
	mu.Unlock()
	if err != nil {
		log.Printf("tokens %s: %v", tokenFile, err)
	}
	reply(w, map[string]bool{"kept": true})
}

// clearLeak: POST /v1/devices/{id}/clear: a phone's all-clear; the device leaves the wake mode at its next check-in
func clearLeak(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	l, open := leaks[id]
	delete(leaks, id)
	// ?not_leak=1: a false alarm (the app's Not a leak): the check is re-armed (notLeak)
	if open && r.URL.Query().Get("not_leak") == "1" {
		notLeak[id] = l.Why
	}
	mu.Unlock()
	if open {
		log.Printf("%s: leak cleared (not a leak: %v)", id, r.URL.Query().Get("not_leak") == "1")
	}
	reply(w, map[string]bool{"cleared": open})
}

// putWifi: PUT /v1/devices/{id}/wifi {"ssid","psk"}: a new network, handed over at the device's next check-in
func putWifi(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	var b struct {
		SSID string `json:"ssid"`
		PSK  string `json:"psk"`
	}
	if !idRe.MatchString(id) || json.NewDecoder(http.MaxBytesReader(w, r.Body, 512)).Decode(&b) != nil ||
		len(b.SSID) < 1 || len(b.SSID) > 32 || len(b.PSK) > 63 || (len(b.PSK) > 0 && len(b.PSK) < 8) {
		http.Error(w, "body {\"ssid\": 1-32 bytes, \"psk\": none or 8-63 bytes}", http.StatusBadRequest)
		return
	}
	mu.Lock()
	wifiChanges[id] = &WifiChange{SSID: b.SSID, PSK: b.PSK, Asked: time.Now()}
	at := nextCheckin(id)
	mu.Unlock()
	log.Printf("%s: Wi-Fi change to %q waits for its next check-in", id, b.SSID)
	reply(w, map[string]any{"pending": true, "at": at})
}

// queueCapture: one of each kind at most (user 2026-11-07: asked again while one waits, that one stays), in Rank
// order; mu held
func queueCapture(id, kind string) {
	q := captures[id]
	if slices.ContainsFunc(q, func(c Capture) bool { return c.Kind == kind }) {
		return
	}
	i := slices.IndexFunc(q, func(c Capture) bool { return Rank[c.Kind] > Rank[kind] })
	if i < 0 {
		i = len(q)
	}
	captures[id] = slices.Insert(q, i, Capture{Kind: kind, Asked: time.Now()})
}

// pending: what waits for the device's check-ins, in the order it gets them (Rank): a Wi-Fi change not yet answered
// ("sent" once handed over), the photos and videos; mu held
func pending(id string) []Pending {
	p := []Pending{}
	for _, c := range captures[id] {
		p = append(p, Pending{Kind: c.Kind, Asked: c.Asked.Unix(), State: "waiting"})
	}
	if c := wifiChanges[id]; c != nil && c.Result == 0 {
		st := "waiting"
		if !c.Delivered.IsZero() {
			st = "sent"
		}
		p = append(p, Pending{Kind: "wifi", SSID: c.SSID, Asked: c.Asked.Unix(), State: st})
	}
	slices.SortStableFunc(p, func(a, b Pending) int { return Rank[a.Kind] - Rank[b.Kind] })
	return p
}

// wifiState: the device's last Wi-Fi change for a client: pending, delivered, saved, failed, "" none; mu held
func wifiState(id string) string {
	c := wifiChanges[id]
	switch {
	case c == nil:
		return ""
	case c.Delivered.IsZero():
		return "pending"
	case c.Result == 0:
		return "delivered"
	case c.Result == WifiSaved:
		return "saved"
	}
	return "failed"
}

// capture: POST /v1/devices/{id}/capture {"kind":"photo"|"video"}: the app's Take photo / Take video for a board not in
// its BLE range, handed over at the board's next check-in (DESIGN.md 5.3.14); queued with the others (pending), one of
// each kind
func capture(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	var b struct {
		Kind string `json:"kind"`
	}
	if !idRe.MatchString(id) || json.NewDecoder(http.MaxBytesReader(w, r.Body, 128)).Decode(&b) != nil ||
		(b.Kind != "photo" && b.Kind != "video") {
		http.Error(w, "body {\"kind\": \"photo\" or \"video\"}", http.StatusBadRequest)
		return
	}
	mu.Lock()
	queueCapture(id, b.Kind)
	within := deviceIntervalsLocked(id).Checkin
	if s := wakeModeS(id); s > 0 {
		within = s
	}
	at := nextCheckin(id)
	mu.Unlock()
	log.Printf("%s: %s asked, at its next check-in", id, b.Kind)
	reply(w, map[string]int64{"within_s": int64(within), "at": at})
}

// mediaIDs: the IDs of the pictures (p1..) and videos (v1) one capture makes: "<device>-<unix s>-p1"; the board
// uploads each under its ID and the app fetches it by it (DESIGN.md 5.3.13, 5.3.14)
func mediaIDs(id, kind string, at time.Time) []string {
	pre := id + "-" + strconv.FormatInt(at.Unix(), 10) + "-"
	switch kind {
	case "photo":
		return []string{pre + "p1"}
	case "video":
		return []string{pre + "v1"}
	}
	ids := make([]string, 0, LeakPhotos+1)
	for n := 1; n <= LeakPhotos; n++ {
		ids = append(ids, pre+"p"+strconv.Itoa(n))
	}
	return append(ids, pre+"v1")
}

// putMedia: POST /v1/media/{id}: the board uploads a picture (JPEG) or video (MP4) under the ID its capture gave it;
// written whole, then renamed (a reader never sees half a file). The first picture (p1) is pushed to the phones
// (`media`): the leak alert still ringing shows it (user 2026-11-07: "one picture downloaded and embedded"). Open on
// the LAN like the other routes until the server authenticates the boards (test109)
func putMedia(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !mediaRe.MatchString(id) {
		http.Error(w, "id: <device>-<unix s>-p<n>|v<n>", http.StatusBadRequest)
		return
	}
	ext := ".jpg"
	if strings.Contains(id[len(id)-3:], "v") {
		ext = ".mp4"
	}
	path := filepath.Join(mediaDir, id+ext)
	f, err := os.CreateTemp(mediaDir, ".upload-*")
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	_, err = io.Copy(f, http.MaxBytesReader(w, r.Body, MediaMax))
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(f.Name(), path)
	}
	if err != nil {
		os.Remove(f.Name())
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	log.Printf("media %s stored", id)
	if strings.HasSuffix(id, "-p1") {
		go notify([]map[string]string{{"type": "media", "id": id[:12], "media": id}})
	}
	reply(w, map[string]bool{"stored": true})
}

// MediaItem: a picture or video a device uploaded, as the app lists it (links, user 2026-11-07)
type MediaItem struct {
	ID   string `json:"id"`
	Kind string `json:"kind"` // "photo" or "video"
	At   int64  `json:"at"`   // unix s: when it was asked for (the ID's time)
}

// deviceMedia: the device's pictures and videos on the server, newest first, at most MediaList
func deviceMedia(id string) []MediaItem {
	files, _ := filepath.Glob(filepath.Join(mediaDir, id+"-*"))
	list := []MediaItem{}
	for _, f := range files {
		name := strings.TrimSuffix(strings.TrimSuffix(filepath.Base(f), ".jpg"), ".mp4")
		if !mediaRe.MatchString(name) {
			continue
		}
		at, _ := strconv.ParseInt(name[13:23], 10, 64)
		kind := "photo"
		if strings.HasSuffix(f, ".mp4") {
			kind = "video"
		}
		list = append(list, MediaItem{ID: name, Kind: kind, At: at})
	}
	slices.SortFunc(list, func(a, b MediaItem) int { return strings.Compare(b.ID, a.ID) })
	return list[:min(len(list), MediaList)]
}

// media: GET /v1/media/{id}: a picture (<id>.jpg) or video (<id>.mp4) the board uploaded, by the ID the capture gave it
// (mediaIDs); 404 until it is there: the app's tile keeps its spinner and asks again
func media(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !mediaRe.MatchString(id) {
		http.Error(w, "id: <device>-<unix s>-p<n>|v<n>", http.StatusBadRequest)
		return
	}
	ext := ".jpg"
	if strings.Contains(id[len(id)-3:], "v") {
		ext = ".mp4"
	}
	path := filepath.Join(mediaDir, id+ext)
	if _, err := os.Stat(path); err != nil {
		http.NotFound(w, r)
		return
	}
	http.ServeFile(w, r, path)
}
