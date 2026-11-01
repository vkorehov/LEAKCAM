// LEAKCAM server, presence only (use case 2): each device checks in with its chip ID (GET /v1/poll?id=), the app asks
// whether a device is online (GET /v1/devices/{id}). State is in memory: after a restart the devices check in again.
package main

import (
	"encoding/json"
	"log"
	"net/http"
	"os"
	"regexp"
	"strconv"
	"sync"
	"time"
)

const (
	PollS = 60 // the check-in interval the server gives the devices (LEAKCAM_POLL_S: shorter for tests)
	Addr  = ":8090"
)

type Device struct {
	ID       string `json:"id"`
	Online   bool   `json:"online"`
	LastSeen int64  `json:"last_seen_s"` // seconds since the last check-in, -1 if never seen
}

var (
	idRe  = regexp.MustCompile(`^[0-9A-F]{12}$`) // the BL616 chip ID: its factory MAC, 12 upper-case hex digits
	mu    sync.Mutex
	seen  = map[string]time.Time{}
	pollS = PollS
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
	seen[id] = time.Now()
	mu.Unlock()
	reply(w, map[string]int{"stream": 0, "poll": pollS})
}

func device(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	t, ok := seen[id]
	mu.Unlock()
	d := Device{ID: id, LastSeen: -1}
	if ok {
		d.LastSeen = int64(time.Since(t).Seconds())
		d.Online = d.LastSeen < int64(3*pollS) // three check-ins missed: offline
	}
	reply(w, d)
}

func main() {
	addr := Addr
	if a := os.Getenv("LEAKCAM_ADDR"); a != "" {
		addr = a
	}
	if p, err := strconv.Atoi(os.Getenv("LEAKCAM_POLL_S")); err == nil && p > 0 {
		pollS = p
	}
	http.HandleFunc("GET /{$}", func(w http.ResponseWriter, r *http.Request) { reply(w, "ok") })
	http.HandleFunc("GET /v1/poll", poll)
	http.HandleFunc("GET /v1/devices/{id}", device)
	log.Printf("listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, nil))
}
