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
	LeakProbeMV = 1100                                  // a probe below this is wet: the default probe threshold (thresholds.h THR_PROBE_MV)
	HumidPct    = 85                                    // %RH or more at any reading: a humid room, the default (DESIGN.md 5.3.15; thresholds.h THR_HUMID)
	WakeModeS   = 300                                   // the check-in interval in the wake mode after a failed Wi-Fi change
	LeakModeS   = 30                                    // during a leak: a request (live video, a photo) starts within half a minute (user 2026-11-13), ~120 mAh a day
	WakeModeMax = 24 * time.Hour                        // the 5-min wake and the focus modes end after this at the latest: ~13 mAh a day
	Tokens      = "/home/pi/leakcam-server/tokens.json" // LEAKCAM_TOKENS: per phone its FCM token
	WifiSaved   = 0x04                                  // wifi_check's state for a network joined, checked and stored
	VideoS      = 15                                    // a video's length (user, 2026-11-07; DESIGN.md 5.3.14)
	MediaList   = 20                                    // the newest pictures and videos listed per device
	MediaMax    = 32 << 20                              // the largest upload (a 15 s video of both cameras)
	LeakPhotos  = 4                                     // every leak is recorded: 4 pictures (2 per camera) and a 15 s video per camera (user, 2026-11-07/12)
	Cameras     = 2                                     // a photo is one picture per camera, a video one video per camera (user, 2026-11-12)
	LeakGapMs   = 2000                                  // a leak's 2 pictures per camera this far apart: flowing water shows as a change (user, 2026-11-12)
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
	// per device when the person last cleared a leak: a camera change counts only from a leak check that ran after it
	// (the job result rides on every check-in for 60 s: the check-in right after a clear opened the same leak again,
	// test140 2026-11-12)
	clearedAt = map[string]time.Time{}
	// per device and camera the problem the phones were told of ("dark", "unfocused", "untuned", "" none): told once
	focusTold = map[string][Cameras]string{}
	// since when a camera of the device is flagged (unfocused / untuned): its 60 s check-ins (focusModeS) for at most
	// WakeModeMax from here
	focusSince = map[string]time.Time{}
	// what a check-in brings first (user 2026-11-07): a leak's record (the emergency), a Wi-Fi change, a photo, a video
	Rank = map[string]int{"leak": 0, "wifi": 1, "photo": 2, "video": 3, "focus": 4, "ignore": 5}
	// per device when the person last asked the board for something from the app (a photo, a video, live video, focus):
	// an open leak's fast check-ins count from it (LeakSteps)
	askedAt = map[string]time.Time{}
	// per device when its last focus / live session ended: camera changes are no leak for FocusQuietS after it
	focusEnded = map[string]time.Time{}
	// per device when its cameras-to-ignore were last handed to the board (reconcileIgnore)
	ignoreSent = map[string]time.Time{}
	tokenFile  = Tokens
)

// leakIn: why this check-in shows a leak ("" none): a wet probe, 85 %RH or more (a warm board only reads lower: any
// reading counts, DESIGN.md 5.3.15), or the leak job's camera change in this check-in
func leakIn(m map[string]any, th Thresholds) string {
	if p, ok := m["probe"].(int64); ok && p >= 0 && p < int64(th.ProbeMV) {
		return "probe wet"
	}
	if h, ok := m["rh"].(int64); ok && h >= int64(th.Humid)*10 {
		return "humidity high"
	}
	if j, ok := m["job"].(string); ok && cameraChanged(j, th.Ignore) {
		if ago, ok := m["jago"].(int64); ok && ago >= 0 && ago <= 60 {
			return "camera change"
		}
	}
	return ""
}

// reconcileIgnore: the board's ignored cameras made the same as the person's (DESIGN.md 5.3.18): the K230 reports each
// ignored camera as "ignored" in its job; a leak check taken after the last hand-over that still differs gets the mask
// again (the K230 was off, the write failed); mu held
func reconcileIgnore(id string, m map[string]any) {
	j, _ := m["job"].(string)
	f := strings.Split(j, ",")
	ago, ok := m["jago"].(int64)
	if len(f) < Cameras || !ok || ago < 0 || time.Since(ignoreSent[id]) < time.Duration(ago)*time.Second {
		return // no job, or one from before the hand-over
	}
	for cam, want := range thresholds[id].Ignore {
		if (f[cam] == "ignored") != want {
			queueCapture(id, "ignore")
			return
		}
	}
}

// cameraChanged: a camera the person did not ignore reads "changed" in the job "<cam0>,<cam1>,..." (a camera that
// sees nothing reads "dark", out of focus "unfocused": neither is a leak, DESIGN.md 5.3.18)
func cameraChanged(job string, ignore [Cameras]bool) bool {
	for cam, v := range strings.Split(job, ",") {
		if v == "changed" && (cam >= Cameras || !ignore[cam]) {
			return true
		}
	}
	return false
}

// checkInAlerts: a check-in's leak and Wi-Fi change result, and its answer's part (mode, a pending network); the
// messages to send once mu is released; mu held
func checkInAlerts(id string, in CheckIn, ans map[string]any) (msgs []map[string]string) {
	m := in.Metrics
	why := leakIn(m, deviceThresholdsLocked(id))
	if ago, ok := m["jago"].(int64); ok && why == "camera change" && !in.At.Add(-time.Duration(ago)*time.Second).After(clearedAt[id]) {
		why = "" // the job that the person already called not a leak
	}
	if why == "camera change" && (time.Since(focusAsked[id]) < FocusAskS*time.Second || time.Since(focusEnded[id]) < FocusQuietS*time.Second) {
		// the person is at the board (focus adjustment or live video asked for, running or just over): hands, a wiped
		// lens, the room's light on are no leak (user 2026-11-13: "no leak detection while focusing and board in live
		// mode"; that evening every such change rang the alarm and held the cameras with the leak's pictures and videos
		// while the person waited for the focus screen). A wet probe or the humidity still alarm
		log.Printf("%s: camera change during a focus / live session: no leak", id)
		why = ""
	}
	switch notLeak[id] {
	case "probe wet", "humidity high": // re-armed once the probe is dry / the humidity below 85 %RH
		if why == notLeak[id] {
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
	if time.Since(focusAsked[id]) >= FocusAskS*time.Second && time.Since(focusEnded[id]) >= FocusQuietS*time.Second {
		// not while the person tunes the focus or watches live (nor FocusQuietS after): told at the first check-in
		// after it, should a camera still need them
		msgs = append(msgs, focusAlerts(id, m, in.SSID)...)
	}
	reconcileIgnore(id, m)
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
		c := map[string]any{"kind": k, "ids": mediaIDs(id, k, in.At), "cams": Cameras} // cams: picture n from camera (n-1) % cams
		switch k {
		case "video":
			c["secs"] = VideoS
		case "leak":
			c["photos"], c["secs"], c["gap_ms"] = LeakPhotos, VideoS, LeakGapMs
		case "focus":
			c["secs"] = FocusMaxS // the session's longest: the board ends it earlier on Stop or no viewer (focus.go)
		case "ignore": // no picture: the mask of the cameras to ignore, which the K230 keeps in its flash (5.3.18)
			mask := 0
			for cam, on := range thresholds[id].Ignore {
				if on {
					mask |= 1 << cam
				}
			}
			c["photos"] = mask
			ignoreSent[id] = time.Now()
		}
		c["more"] = len(q) - 1 // waiting after this one: the board checks in again right after its SENT
		ans["capture"] = c
		if captures[id] = q[1:]; len(captures[id]) == 0 {
			delete(captures, id)
		}
		log.Printf("%s: %s handed over, %d more waiting", id, k, len(q)-1)
	}
	if s := wakeModeS(id); s > 0 {
		ans["checkin"] = s
		ans["mode"] = 1
	} else if s := focusModeS(id); s > 0 && s < deviceIntervalsLocked(id).Checkin {
		// a camera to tune: sooner check-ins, not the wake mode's BLE (5.3.17); only ever sooner: a board on a
		// shorter interval of its own keeps it (2026-11-14: the tests' 5 s became 60 s on a board whose cameras were
		// never tuned, test134 / test108 / test117 failed)
		ans["checkin"] = s
		ans["mode"] = 0
	} else {
		ans["mode"] = 0
	}
	return msgs
}

// focusAlerts: a camera that sees nothing ("dark", DESIGN.md 5.3.18), out of focus, or never tuned (DESIGN.md 5.3.17):
// the job's "<cam0>,<cam1>,<foc0>,<foc1>" (dark: its LED-lit picture below the K230's JOB_DARK_MEAN; unfocused: FOCUS_BAD_N LED-lit checks below 60 % of the camera's reference; foc -1: no reference yet) -> one "focus"
// message per camera and reason (a normal notification, not the alarm: it opens the focus screen); again only after
// the camera was fine in between; mu held
func focusAlerts(id string, m map[string]any, ssid string) (msgs []map[string]string) {
	j, _ := m["job"].(string)
	f := strings.Split(j, ",")
	if len(f) < 2*Cameras {
		return nil // an older board: no focus fields
	}
	told := focusTold[id]
	ignore := thresholds[id].Ignore
	for cam := 0; cam < Cameras; cam++ {
		why := ""
		switch {
		case ignore[cam]: // the person's choice: nothing told about it
		case f[cam] == "dark": // sees nothing, the LEDs on: covered, facing nothing in reach, far out of focus
			why = "dark"
		case f[cam] == "unfocused":
			why = "unfocused"
		case f[Cameras+cam] == "-1" && f[cam] != "nocam":
			why = "untuned"
		}
		if why != "" && told[cam] != why {
			log.Printf("%s: camera %d %s", id, cam+1, why)
			msgs = append(msgs, map[string]string{"type": "focus", "id": id, "cam": strconv.Itoa(cam), "why": why,
				"ssid": ssid})
		}
		told[cam] = why
	}
	focusTold[id] = told
	if told == ([Cameras]string{}) {
		delete(focusSince, id)
	} else if _, ok := focusSince[id]; !ok {
		focusSince[id] = time.Now()
	}
	return msgs
}

// LeakSteps: an open leak's check-in interval by the time since the leak opened or the person last asked the board
// for something (user 2026-11-13: "board sleeps only 30 seconds ... if no response in 1 day then start sleeping one
// minute then 5 minutes in one more day, i.e. exponential"): every 30 s the first day, every minute the second, every
// 5 min the third, then the board's normal check-in
var LeakSteps = []struct {
	Until time.Duration
	S     int
}{{24 * time.Hour, LeakModeS}, {48 * time.Hour, 60}, {72 * time.Hour, 300}}

// wakeModeS: the check-in interval while the device stays reachable (BLE sleep between check-ins), 0 when it is not:
// an open leak by LeakSteps (until the person's Not a leak or all clear), a failed Wi-Fi change every 5 min for at
// most WakeModeMax; mu held
func wakeModeS(id string) int {
	if l, open := leaks[id]; open {
		from := l.Since
		if t := askedAt[id]; t.After(from) {
			from = t // a response: the fast pace again
		}
		for _, st := range LeakSteps {
			if time.Since(from) < st.Until {
				return st.S
			}
		}
	}
	if c := wifiChanges[id]; c != nil && c.Result != 0 && c.Result != WifiSaved && time.Since(c.Delivered) < WakeModeMax {
		return WakeModeS
	}
	return 0
}

// focusModeS: FocusModeS while a camera is flagged (for at most WakeModeMax) or a focus session was asked for in the
// last FocusAskS: the person tuning the lens waits under a minute for it (user 2026-11-13: "faster check-ins"; DESIGN.md
// 5.3.17), 0 otherwise; mu held
func focusModeS(id string) int {
	if t, ok := focusSince[id]; ok && time.Since(t) < WakeModeMax {
		return FocusModeS
	}
	if t, ok := focusAsked[id]; ok && time.Since(t) < FocusAskS*time.Second {
		return FocusModeS
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
	if open {
		clearedAt[id] = time.Now() // the job results before it no longer open a leak
		// its record not taken yet is moot: the next photo is not held behind it (test143, 2026-11-12)
		captures[id] = slices.DeleteFunc(captures[id], func(c Capture) bool { return c.Kind == "leak" })
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
	askedAt[id] = time.Now()
	within := deviceIntervalsLocked(id).Checkin
	if s := wakeModeS(id); s > 0 {
		within = s
	} else if s := focusModeS(id); s > 0 && s < within {
		within = s
	}
	at := nextCheckin(id)
	mu.Unlock()
	log.Printf("%s: %s asked, at its next check-in", id, b.Kind)
	reply(w, map[string]int64{"within_s": int64(within), "at": at})
}

// mediaIDs: the IDs of the pictures (p1..) and videos (v1..) one capture makes: "<device>-<unix s>-p1"; the board
// uploads each under its ID and the app fetches it by it (DESIGN.md 5.3.13, 5.3.14). Separate files per camera (user,
// 2026-11-12: "two separate pictures from both cameras, two separate videos"): picture n is camera (n-1) % Cameras
// (p1 J4, p2 J5, p3 J4, ...), video n is camera n-1. A photo: p1, p2; a video: v1, v2; a leak: p1..p4, v1, v2
func mediaIDs(id, kind string, at time.Time) []string {
	pre := id + "-" + strconv.FormatInt(at.Unix(), 10) + "-"
	photos, videos := 0, 0
	switch kind {
	case "focus", "ignore":
		return []string{pre + kind[:1]} // no file (focus: the session's stream, focus.go; ignore: a setting); the ID gives the board its device and time
	case "photo":
		photos = Cameras
	case "video":
		videos = Cameras
	default:
		photos, videos = LeakPhotos, Cameras
	}
	ids := make([]string, 0, photos+videos)
	for n := 1; n <= photos; n++ {
		ids = append(ids, pre+"p"+strconv.Itoa(n))
	}
	for n := 1; n <= videos; n++ {
		ids = append(ids, pre+"v"+strconv.Itoa(n))
	}
	return ids
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
	if ext == ".mp4" && r.Header.Get("Content-Type") == VideoType {
		putVideo(w, r, id, path) // the board's H.265 as it is encoded: live HLS, then the MP4 (video.go)
		return
	}
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
	// the moment it was taken (user 2026-11-12: "timestamp in picture naming, also in metadata"): the board's X-Taken
	// (unix ms) as the file's time, handed to the app on download (media); without it the upload's time stays
	if ms, perr := strconv.ParseInt(r.Header.Get("X-Taken"), 10, 64); perr == nil && ms > 0 {
		t := time.UnixMilli(ms)
		os.Chtimes(path, t, t)
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
	st, err := os.Stat(path)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("X-Taken", strconv.FormatInt(st.ModTime().UnixMilli(), 10)) // when it was taken (putMedia)
	http.ServeFile(w, r, path)
}
