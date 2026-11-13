package main

// The focus session through the server (DESIGN.md 5.3.17; user 2026-11-13: "focus tuning is done on already setup
// phone", "live stream video", "quickly switch camera and see instantly video changing"). The app's Tune focus asks for
// it (POST /v1/devices/{id}/focus), the board's next check-in gets it as a capture of kind "focus" (60 s check-ins
// while a camera is flagged or a session was asked for: focusModeS), and then
//   - the board streams: POST /v1/focus/{id}/stream, chunked, records of [u32 BE length | kind][u64 BE us][payload]:
//     kind 0 one H.265 access unit (Annex-B) of the shown camera, FocusText a text line (FOC,<cam>,<seq>,<score> per
//     frame; FOCUS,OK,<cam>,<score> after a Done);
//   - the server relays every record as it comes to the app's GET /v1/focus/{id}/live (the same records, chunked; no
//     HLS: the picture follows the lens, HLS trails 2-4 s). A viewer that joins late gets the parameter sets first and
//     the pictures from the next IDR; a slow viewer loses pictures up to the next IDR, never the board's stream;
//   - the app's POST /v1/focus/{id}/cmd ({"cam":n} | {"done":<score>} | {"stop":true}) waits for the board's long poll
//     GET /v1/focus/{id}/cmd (the next command at once, else {} after focusPollMax; {"stop":true} when no viewer was
//     there for focusIdleMax or the session ran FocusMaxS).

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"sync"
	"time"
)

const (
	FocusText  = 1 << 30 // a record's kind bit: a text line, else an access unit
	FocusMaxS  = 600     // a session's longest (the check-in answer's secs)
	FocusModeS = 60      // the check-in interval while a camera is flagged or a session is asked for
	FocusAskS  = 600     // an asked-for session keeps the 60 s check-ins this long
	FocusQueue = 64      // records waiting for a viewer before it loses pictures (~4 s of video)
)

var (
	focusIdleMax = 20 * time.Second // no viewer this long: the board is told to stop (tests shorten it)
	focusPollMax = 5 * time.Second  // the board's long poll is answered after this with {}
	focusMu      sync.Mutex
	focuses      = map[string]*focusSession{}
	focusAsked   = map[string]time.Time{} // the device's last Tune focus (focusModeS); mu held
)

type focusSession struct {
	id       string
	cam      int         // the camera the app shows
	cmds     chan []byte // for the board's long poll: JSON commands
	viewers  map[*focusViewer]bool
	params   []byte    // the last parameter sets (VPS, SPS, PPS) as one record: a late viewer's first
	idle     time.Time // since when no viewer watches (zero: one watches)
	started  time.Time // the board's stream began (zero: not yet)
	streamed bool      // a board stream runs now
}

type focusViewer struct {
	out      chan []byte
	needsIDR bool // pictures skipped until the next IDR (joined late, or lost some)
}

// session: the device's focus session, made when first asked for; focusMu held
func session(id string) *focusSession {
	s := focuses[id]
	if s == nil {
		s = &focusSession{id: id, cmds: make(chan []byte, 16), viewers: map[*focusViewer]bool{}, idle: time.Now()}
		focuses[id] = s
	}
	return s
}

// askFocus: POST /v1/devices/{id}/focus {"cam":n}: the app's Tune focus; the board joins at its next check-in (60 s
// while flagged); the answer as capture's: within_s, at
func askFocus(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	var b struct {
		Cam *int `json:"cam"`
	}
	if !idRe.MatchString(id) || json.NewDecoder(http.MaxBytesReader(w, r.Body, 64)).Decode(&b) != nil || b.Cam == nil ||
		*b.Cam < 0 || *b.Cam >= Cameras {
		http.Error(w, "body {\"cam\": 0 or 1}", http.StatusBadRequest)
		return
	}
	focusMu.Lock()
	s := session(id)
	s.cam = *b.Cam
	running := s.streamed
	focusMu.Unlock()
	mu.Lock()
	if !running {
		queueCapture(id, "focus")
	}
	focusAsked[id] = time.Now()
	within := deviceIntervalsLocked(id).Checkin
	if s := wakeModeS(id); s > 0 {
		within = s
	} else if _, flagged := focusSince[id]; flagged {
		within = FocusModeS // a flagged camera: the board was told 60 s at its last check-in
	}
	at := nextCheckin(id)
	mu.Unlock()
	log.Printf("%s: focus asked, camera %d (%s)", id, *b.Cam+1, map[bool]string{true: "running", false: "next check-in"}[running])
	reply(w, map[string]any{"within_s": within, "at": at, "running": running})
}

// focusCmd: POST /v1/focus/{id}/cmd from the app: a switch, Done or Stop for the board's next poll
func focusCmd(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 128))
	var b struct {
		Cam  *int     `json:"cam"`
		Done *float64 `json:"done"`
		Stop bool     `json:"stop"`
	}
	if !idRe.MatchString(id) || err != nil || json.Unmarshal(body, &b) != nil ||
		(b.Cam == nil && b.Done == nil && !b.Stop) || (b.Cam != nil && (*b.Cam < 0 || *b.Cam >= Cameras)) {
		http.Error(w, `body {"cam":n} | {"done":<score>} | {"stop":true}`, http.StatusBadRequest)
		return
	}
	focusMu.Lock()
	s := session(id)
	if b.Cam != nil {
		s.cam = *b.Cam
	}
	// to the board in one fixed form, whatever the client's JSON looked like: the K230 finds "cam":, "done": and
	// "stop":true as text (2026-11-13: a test's {"stop": true}, with a space, was never seen by the board)
	var cmd []byte
	switch {
	case b.Stop:
		cmd = []byte(`{"stop":true}`)
	case b.Done != nil:
		cmd = []byte(fmt.Sprintf(`{"done":%.3f}`, *b.Done))
	default:
		cmd = []byte(fmt.Sprintf(`{"cam":%d}`, *b.Cam))
	}
	select {
	case s.cmds <- cmd:
	default:
		focusMu.Unlock()
		http.Error(w, "too many commands waiting", http.StatusServiceUnavailable)
		return
	}
	focusMu.Unlock()
	if b.Stop {
		mu.Lock()
		delete(focusAsked, id) // the 60 s check-ins only while a camera is flagged now
		captures[id] = deleteKind(captures[id], "focus")
		mu.Unlock()
	}
	reply(w, map[string]bool{"queued": true})
}

// focusPoll: GET /v1/focus/{id}/cmd from the board: the next command, {} after focusPollMax; the first poll of a stream
// gets the camera to show; stop when nobody watched for focusIdle or the session is FocusMaxS old
func focusPoll(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	focusMu.Lock()
	s := focuses[id]
	if s == nil {
		focusMu.Unlock()
		reply(w, map[string]bool{"stop": true}) // no session (the server restarted, or it ended)
		return
	}
	stop := (!s.idle.IsZero() && time.Since(s.idle) > focusIdleMax) ||
		(!s.started.IsZero() && time.Since(s.started) > FocusMaxS*time.Second)
	cmds := s.cmds
	focusMu.Unlock()
	if stop {
		reply(w, map[string]bool{"stop": true})
		return
	}
	select {
	case c := <-cmds:
		w.Header().Set("Content-Type", "application/json")
		w.Write(c)
	case <-time.After(focusPollMax):
		reply(w, map[string]any{})
	case <-r.Context().Done():
	}
}

// focusStream: POST /v1/focus/{id}/stream from the board: every record to every viewer as it comes
func focusStream(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id", http.StatusBadRequest)
		return
	}
	focusMu.Lock()
	s := session(id)
	s.streamed, s.started = true, time.Now()
	start := []byte(fmt.Sprintf(`{"cam":%d}`, s.cam)) // the board's first poll: the camera to show
	select {
	case s.cmds <- start:
	default:
	}
	focusMu.Unlock()
	log.Printf("%s: focus stream from the board", id)
	n, err := relay(s, r.Body)
	focusMu.Lock()
	s.streamed = false
	for v := range s.viewers {
		close(v.out)
		delete(s.viewers, v)
	}
	delete(focuses, id)
	focusMu.Unlock()
	mu.Lock()
	delete(focusAsked, id)
	mu.Unlock()
	log.Printf("%s: focus stream ended: %d records, %v", id, n, err)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	reply(w, map[string]int{"records": n})
}

// relay: record after record from the board to the viewers; the parameter sets kept for late viewers
func relay(s *focusSession, body io.Reader) (int, error) {
	var hdr [12]byte
	n := 0
	for {
		if _, err := io.ReadFull(body, hdr[:]); err != nil {
			if errors.Is(err, io.EOF) {
				return n, nil
			}
			return n, err
		}
		l := binary.BigEndian.Uint32(hdr[:4])
		size := l &^ FocusText
		if size == 0 || size > VideoFrameMax {
			return n, fmt.Errorf("record %d: length %d", n, size)
		}
		rec := make([]byte, 12+size)
		copy(rec, hdr[:])
		if _, err := io.ReadFull(body, rec[12:]); err != nil {
			return n, err
		}
		n++
		text := l&FocusText != 0
		// a random-access picture (IDR 19/20 from the K230, CRA 21 from libx265, BLA 16-18): a viewer starts there
		idr := !text && hasNAL(rec[12:], 16, 17, 18, 19, 20, 21)
		params := !text && hasNAL(rec[12:], 32)
		picture := !text && hasNAL(rec[12:], vclTypes...)
		focusMu.Lock()
		if params {
			s.params = paramsRecord(rec)
		}
		for v := range s.viewers {
			if picture && v.needsIDR {
				if !idr {
					continue
				}
				v.needsIDR = false
			}
			select {
			case v.out <- rec:
			default:
				if picture {
					v.needsIDR = true // the viewer is behind: pictures again from the next IDR
				}
			}
		}
		focusMu.Unlock()
	}
}

// focusLive: GET /v1/focus/{id}/live from the app: the board's records as they come, chunked; waits for the board
// (the session is asked for, it joins at its check-in); ends with the board's stream
func focusLive(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id", http.StatusBadRequest)
		return
	}
	fl, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "no streaming", http.StatusInternalServerError)
		return
	}
	v := &focusViewer{out: make(chan []byte, FocusQueue), needsIDR: true}
	focusMu.Lock()
	s := session(id)
	if s.params != nil {
		v.out <- s.params
	}
	s.viewers[v] = true
	s.idle = time.Time{}
	focusMu.Unlock()
	defer func() {
		focusMu.Lock()
		if s.viewers[v] {
			delete(s.viewers, v)
		}
		if len(s.viewers) == 0 {
			s.idle = time.Now()
		}
		focusMu.Unlock()
	}()
	w.Header().Set("Content-Type", "application/x-leakcam-focus")
	w.WriteHeader(http.StatusOK)
	fl.Flush()
	for {
		select {
		case rec, open := <-v.out:
			if !open {
				return // the board's stream ended
			}
			if _, err := w.Write(rec); err != nil {
				return
			}
			fl.Flush()
		case <-r.Context().Done():
			return
		}
	}
}

// vclTypes: the H.265 NAL types that carry a picture's slices
var vclTypes = []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21}

// hasNAL: an access unit (Annex-B) with a NAL unit of one of these H.265 types in its first 4 KB (the parameter sets
// and the first slice's header come first)
func hasNAL(au []byte, types ...int) bool {
	head := au[:min(len(au), 4096)]
	for i := 0; i+3 < len(head); i++ {
		if head[i] == 0 && head[i+1] == 0 && head[i+2] == 1 {
			t := int(head[i+3]>>1) & 0x3f
			for _, want := range types {
				if t == want {
					return true
				}
			}
		}
	}
	return false
}

// paramsRecord: the VPS, SPS and PPS of an access unit as a record of their own (kind 0, the same time)
func paramsRecord(rec []byte) []byte {
	au := rec[12:]
	var out bytes.Buffer
	starts := []int{}
	for i := 0; i+3 < len(au) && i < 4096; i++ {
		if au[i] == 0 && au[i+1] == 0 && au[i+2] == 1 {
			starts = append(starts, i+3)
		}
	}
	for k, a := range starts {
		end := len(au)
		if k+1 < len(starts) {
			end = starts[k+1] - 3
		}
		if t := int(au[a]>>1) & 0x3f; t >= 32 && t <= 34 {
			out.Write([]byte{0, 0, 0, 1})
			out.Write(bytes.TrimRight(au[a:end], "\x00"))
		}
	}
	p := make([]byte, 12+out.Len())
	binary.BigEndian.PutUint32(p, uint32(out.Len()))
	copy(p[4:12], rec[4:12])
	copy(p[12:], out.Bytes())
	return p
}

// deleteKind: the queue without the waiting captures of one kind
func deleteKind(q []Capture, kind string) []Capture {
	out := q[:0]
	for _, c := range q {
		if c.Kind != kind {
			out = append(out, c)
		}
	}
	return out
}
