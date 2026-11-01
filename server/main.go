// LEAKCAM server, presence only (use case 2): each device checks in with its chip ID (GET /v1/poll?id=), the app asks
// whether a device is online (GET /v1/devices/{id}) and can wake its BLE (POST /v1/devices/{id}/wake: the next check-in
// answers "ble":1, the board advertises for 2 minutes, tests/test119). State is in memory: after a restart the devices
// check in again.
package main

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"regexp"
	"strconv"
	"sync"
	"time"
)

const (
	PollS   = 60 // the check-in interval the server gives the devices (LEAKCAM_POLL_S: shorter for tests)
	Addr    = ":8090"
	WakeGap = 2 * time.Minute // at most one BLE wake per device this often: a wake costs its battery 2 minutes awake
)

type Device struct {
	ID       string `json:"id"`
	Online   bool   `json:"online"`
	LastSeen int64  `json:"last_seen_s"` // seconds since the last check-in, -1 if never seen
	SSID     string `json:"ssid"`        // the network it checked in from (not secret; the passphrase never comes)
	Wake     bool   `json:"wake"`        // a BLE wake waits for its next check-in
}

type CheckIn struct {
	At   time.Time
	SSID string
}

var (
	idRe   = regexp.MustCompile(`^[0-9A-F]{12}$`) // the BL616 chip ID: its factory MAC, 12 upper-case hex digits
	mu     sync.Mutex
	seen   = map[string]CheckIn{}
	wake   = map[string]bool{}      // asked, not yet delivered
	woken  = map[string]time.Time{} // the last wake delivered
	pollS  = PollS
	pinKey []byte // the BLE pairing PIN's key, shared with the firmware (LEAKCAM_PIN_KEY); never in the app
)

func reply(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(v); err != nil {
		log.Printf("reply: %v", err)
	}
}

func poll(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Query().Get("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	seen[id] = CheckIn{time.Now(), r.URL.Query().Get("ssid")}
	ble := 0
	if wake[id] {
		ble = 1
		delete(wake, id)
		woken[id] = time.Now()
	}
	mu.Unlock()
	reply(w, map[string]int{"stream": 0, "poll": pollS, "ble": ble})
}

func device(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	c, ok := seen[id]
	d := Device{ID: id, LastSeen: -1, Wake: wake[id]}
	mu.Unlock()
	if ok {
		d.LastSeen = int64(time.Since(c.At).Seconds())
		d.SSID = c.SSID
		d.Online = d.LastSeen < int64(3*pollS) // three check-ins missed: offline
	}
	reply(w, d)
}

// wakeUp: the board advertises over BLE after its next check-in, for a phone nearby that wants to set its Wi-Fi while it
// is out of range (a configured board on battery advertises only when its presence is lost); refused (429) within
// WakeGap of the last one delivered. It only makes the board advertise: pairing still needs the PIN
func wakeUp(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	defer mu.Unlock()
	if t, ok := woken[id]; ok && time.Since(t) < WakeGap {
		http.Error(w, "woken less than 2 minutes ago", http.StatusTooManyRequests)
		return
	}
	wake[id] = true
	reply(w, map[string]int{"within_s": pollS})
}

// pin: the board's BLE pairing passkey, HMAC-SHA256(key, the 6 ID bytes), first 4 bytes big-endian mod 10^6, as
// firmware/bl616/ble_pairing.c derives it; the app asks for it right before pairing. Open on the LAN until the
// server authenticates the app (test109)
func pin(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	if pinKey == nil {
		http.Error(w, "no pairing key configured", http.StatusServiceUnavailable)
		return
	}
	b, err := hex.DecodeString(id)
	if err != nil {
		http.Error(w, "id", http.StatusBadRequest)
		return
	}
	m := hmac.New(sha256.New, pinKey)
	m.Write(b)
	reply(w, map[string]string{"pin": fmt.Sprintf("%06d", binary.BigEndian.Uint32(m.Sum(nil)[:4])%1000000)})
}

func main() {
	addr := Addr
	if a := os.Getenv("LEAKCAM_ADDR"); a != "" {
		addr = a
	}
	if p, err := strconv.Atoi(os.Getenv("LEAKCAM_POLL_S")); err == nil && p > 0 {
		pollS = p
	}
	if k, err := hex.DecodeString(os.Getenv("LEAKCAM_PIN_KEY")); err == nil && len(k) == 32 {
		pinKey = k
	} else {
		log.Printf("no LEAKCAM_PIN_KEY (64 hex digits): /v1/devices/{id}/pin answers 503")
	}
	http.HandleFunc("GET /{$}", func(w http.ResponseWriter, r *http.Request) { reply(w, "ok") })
	http.HandleFunc("GET /v1/poll", poll)
	http.HandleFunc("GET /v1/devices/{id}", device)
	http.HandleFunc("GET /v1/devices/{id}/pin", pin)
	http.HandleFunc("POST /v1/devices/{id}/wake", wakeUp)
	log.Printf("listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, nil))
}
