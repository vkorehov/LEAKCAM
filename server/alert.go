package main

// alert: the leak alert, the Wi-Fi change through the server and the 5-min wake mode (DESIGN.md 5.3.13)

import (
	"encoding/json"
	"log"
	"net/http"
	"strconv"
	"strings"
	"time"
)

const (
	LeakProbeMV = 1100                                  // a probe below this is wet: the firmware's comparator reference (leak_check.c VIO_LEAK)
	WakeModeS   = 300                                   // the check-in interval in the 5-min wake mode
	WakeModeMax = 24 * time.Hour                        // the mode ends after this at the latest: it costs the battery ~13 mAh a day
	Tokens      = "/home/pi/leakcam-server/tokens.json" // LEAKCAM_TOKENS: per phone its FCM token
	WifiSaved   = 0x04                                  // wifi_check's state for a network joined, checked and stored
	VideoS      = 15                                    // a video's length (user, 2026-11-07; DESIGN.md 5.3.14)
)

// Leak: a device's open leak, until a phone's all-clear
type Leak struct {
	Since time.Time `json:"since"`
	Why   string    `json:"why"`
}

// WifiChange: a new network for a device, handed over at its next check-in; the passphrase forgotten once delivered
type WifiChange struct {
	SSID      string
	PSK       string
	Delivered time.Time
	Result    int // the board's wifi_check state for it (wchg), 0 = none yet
}

var (
	leaks       = map[string]Leak{}
	wifiChanges = map[string]*WifiChange{}
	tokens      = map[string]string{} // phone -> FCM token
	captures    = map[string]string{} // per device a photo or video asked for, handed over at its next check-in
	tokenFile   = Tokens
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
	if why := leakIn(m); why != "" {
		if _, open := leaks[id]; !open {
			leaks[id] = Leak{Since: time.Now(), Why: why}
			log.Printf("%s: leak (%s)", id, why)
			// what the alert shows: which LEAKCAM (its ID, its network), what found it, when
			rssi := ""
			if v, ok := m["rssi"].(int64); ok {
				rssi = strconv.FormatInt(v, 10)
			}
			msgs = append(msgs, map[string]string{"type": "leak", "id": id, "why": why, "ssid": in.SSID,
				"rssi": rssi, "at": strconv.FormatInt(in.At.Unix(), 10)})
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
	if c := wifiChanges[id]; c != nil && c.Delivered.IsZero() {
		ans["wifi"] = map[string]string{"ssid": c.SSID, "psk": c.PSK}
		c.Delivered, c.PSK = time.Now(), ""
	}
	if k := captures[id]; k != "" {
		c := map[string]any{"kind": k}
		if k == "video" {
			c["secs"] = VideoS
		}
		ans["capture"] = c
		delete(captures, id)
		log.Printf("%s: %s handed over", id, k)
	}
	if wakeMode(id) {
		ans["checkin"] = WakeModeS
		ans["mode"] = 1
	} else {
		ans["mode"] = 0
	}
	return msgs
}

// wakeMode: the device stays reachable (BLE sleep, a check-in every 5 min): an open leak or a failed Wi-Fi change,
// for at most WakeModeMax; mu held
func wakeMode(id string) bool {
	if l, open := leaks[id]; open && time.Since(l.Since) < WakeModeMax {
		return true
	}
	c := wifiChanges[id]
	return c != nil && c.Result != 0 && c.Result != WifiSaved && time.Since(c.Delivered) < WakeModeMax
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
	_, open := leaks[id]
	delete(leaks, id)
	mu.Unlock()
	if open {
		log.Printf("%s: leak cleared", id)
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
	wifiChanges[id] = &WifiChange{SSID: b.SSID, PSK: b.PSK}
	mu.Unlock()
	log.Printf("%s: Wi-Fi change to %q waits for its next check-in", id, b.SSID)
	reply(w, map[string]bool{"pending": true})
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
// its BLE range, handed over at the board's next check-in (DESIGN.md 5.3.14); a second ask replaces the first
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
	captures[id] = b.Kind
	within := deviceIntervalsLocked(id).Checkin
	if wakeMode(id) {
		within = WakeModeS
	}
	mu.Unlock()
	log.Printf("%s: %s asked, at its next check-in", id, b.Kind)
	reply(w, map[string]int{"within_s": within})
}
