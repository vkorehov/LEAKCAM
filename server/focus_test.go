package main

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"testing"
	"time"
)

// the focus session (focus.go, DESIGN.md 5.3.17): asked for, handed over at the check-in, 60 s check-ins while a
// camera is flagged
func TestFocusCheckin(t *testing.T) {
	s := testServer(t)
	ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "job": {"same,unfocused,100,30"}})
	if ans["checkin"] != float64(FocusModeS) || ans["mode"] != float64(0) {
		t.Errorf("a flagged camera: checkin %v mode %v, want %d s without the wake mode's BLE", ans["checkin"],
			ans["mode"], FocusModeS)
	}
	r := must(http.Post(s.URL+"/v1/devices/"+TestID+"/focus", "application/json", strings.NewReader(`{"cam":1}`)))
	if r.StatusCode != http.StatusOK {
		t.Fatal(r.Status)
	}
	if r := must(http.Post(s.URL+"/v1/devices/"+TestID+"/focus", "application/json", strings.NewReader(`{"cam":2}`))); r.StatusCode != http.StatusBadRequest {
		t.Errorf("camera 2 (of 0, 1): %s", r.Status)
	}
	ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "job": {"same,unfocused,100,30"}})
	c, _ := ans["capture"].(map[string]any)
	if c == nil || c["kind"] != "focus" || c["secs"] != float64(FocusMaxS) || idEnds(ans["capture"]) != "f" {
		t.Fatalf("the session at the check-in: %v", ans["capture"])
	}
	// tuned: no flag, no session asked any more -> the normal interval
	mu.Lock()
	delete(focusAsked, TestID)
	mu.Unlock()
	ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "job": {"same,same,100,100"}})
	if ans["checkin"] == float64(FocusModeS) {
		t.Errorf("tuned: still %v s check-ins", ans["checkin"])
	}
}

// focusRecords: the clip (testdata/clip.h265, framed) as the board sends a session: a score line before each picture
func focusRecords(t *testing.T) [][]byte {
	body, _ := framed(t)
	var recs [][]byte
	seq := 0
	for b := body; len(b) > 12; {
		n := 12 + int(binary.BigEndian.Uint32(b))
		au := b[:n]
		b = b[n:]
		line := []byte("FOC,0," + strconv.Itoa(seq) + ",1.234")
		r := make([]byte, 12+len(line))
		binary.BigEndian.PutUint32(r, uint32(len(line))|FocusText)
		copy(r[4:12], au[4:12])
		copy(r[12:], line)
		recs = append(recs, r, au)
		seq++
	}
	return recs
}

// readRecords: the records of a live answer until it ends: (text?, payload)
func readRecords(body io.Reader, out chan<- [2][]byte) {
	defer close(out)
	var hdr [12]byte
	for {
		if _, err := io.ReadFull(body, hdr[:]); err != nil {
			return
		}
		l := binary.BigEndian.Uint32(hdr[:4])
		p := make([]byte, l&^FocusText)
		if _, err := io.ReadFull(body, p); err != nil {
			return
		}
		kind := []byte{0}
		if l&FocusText != 0 {
			kind[0] = 1
		}
		out <- [2][]byte{kind, p}
	}
}

func TestFocusRelay(t *testing.T) {
	s := testServer(t)
	recs := focusRecords(t)
	live := func() chan [2][]byte {
		r, err := http.Get(s.URL + "/v1/focus/" + TestID + "/live")
		if err != nil || r.StatusCode != http.StatusOK {
			t.Fatalf("live: %v %v", err, r)
		}
		ch := make(chan [2][]byte, 1000)
		go readRecords(r.Body, ch)
		return ch
	}
	first := live()
	pr, pw := io.Pipe()
	done := make(chan *http.Response, 1)
	go func() {
		r, _ := http.Post(s.URL+"/v1/focus/"+TestID+"/stream", "application/x-leakcam-focus", pr)
		done <- r
	}()
	var late chan [2][]byte
	for i, r := range recs {
		pw.Write(r)
		time.Sleep(2 * time.Millisecond)
		if i == 2*20 { // a viewer joins at picture 20 (a P frame): the parameter sets, then pictures from an IDR
			time.Sleep(50 * time.Millisecond)
			late = live()
			time.Sleep(50 * time.Millisecond)
		}
	}
	pw.Close()
	if r := <-done; r == nil || r.StatusCode != http.StatusOK {
		t.Fatalf("the board's stream: %v", r)
	}
	pics, lines := 0, 0
	for rec := range first {
		if rec[0][0] == 1 {
			lines++
		} else {
			pics++
		}
	}
	if pics != len(recs)/2 || lines != len(recs)/2 {
		t.Errorf("the first viewer: %d access units, %d score lines, want %d each", pics, lines, len(recs)/2)
	}
	var lateRecs [][2][]byte
	for rec := range late {
		lateRecs = append(lateRecs, rec)
	}
	var lateAUs [][]byte
	for _, rec := range lateRecs {
		if rec[0][0] == 0 {
			lateAUs = append(lateAUs, rec[1])
		}
	}
	var latePics [][]byte // the late viewer's pictures (not the parameter sets alone)
	for _, au := range lateAUs[1:] {
		if hasNAL(au, vclTypes...) {
			latePics = append(latePics, au)
		}
	}
	if len(lateAUs) < 2 || !hasNAL(lateAUs[0], 32) || hasNAL(lateAUs[0], vclTypes...) || len(latePics) == 0 ||
		!hasNAL(latePics[0], 16, 17, 18, 19, 20, 21) {
		t.Fatalf("the late viewer: %d pictures; the first must be the parameter sets alone, the next an IDR", len(lateAUs))
	}
	if !bytes.Contains(lateAUs[0], []byte{0, 0, 0, 1}) {
		t.Errorf("parameter sets without start codes")
	}
}

func TestFocusCmd(t *testing.T) {
	s := testServer(t)
	focusPollMax, focusIdleMax = 200*time.Millisecond, 400*time.Millisecond
	t.Cleanup(func() { focusPollMax, focusIdleMax = 5*time.Second, 20*time.Second })
	poll := func() map[string]any {
		r, err := http.Get(s.URL + "/v1/focus/" + TestID + "/cmd")
		if err != nil {
			t.Fatal(err)
		}
		var m map[string]any
		json.NewDecoder(r.Body).Decode(&m)
		r.Body.Close()
		return m
	}
	if m := poll(); m["stop"] != true {
		t.Errorf("no session: %v, want stop", m)
	}
	must(http.Post(s.URL+"/v1/devices/"+TestID+"/focus", "application/json", strings.NewReader(`{"cam":1}`)))
	r, err := http.Get(s.URL + "/v1/focus/" + TestID + "/live") // the app watches (waits for the board)
	if err != nil {
		t.Fatal(err)
	}
	defer r.Body.Close()
	pr, pw := io.Pipe()
	defer pw.Close()
	go http.Post(s.URL+"/v1/focus/"+TestID+"/stream", "application/x-leakcam-focus", pr)
	time.Sleep(100 * time.Millisecond)
	if m := poll(); m["cam"] != float64(1) {
		t.Errorf("the board's first poll: %v, want the asked camera 1", m)
	}
	must(http.Post(s.URL+"/v1/focus/"+TestID+"/cmd", "application/json", strings.NewReader(`{"cam":0}`)))
	if m := poll(); m["cam"] != float64(0) {
		t.Errorf("the switch: %v", m)
	}
	t0 := time.Now()
	if m := poll(); len(m) != 0 || time.Since(t0) < 150*time.Millisecond {
		t.Errorf("nothing waiting: %v after %v, want {} after the long poll", m, time.Since(t0))
	}
	must(http.Post(s.URL+"/v1/focus/"+TestID+"/cmd", "application/json", strings.NewReader(`{"done":2.5}`)))
	if m := poll(); m["done"] != 2.5 {
		t.Errorf("Done: %v", m)
	}
	// the board reads the command as text: one form whatever the client sent
	must(http.Post(s.URL+"/v1/focus/"+TestID+"/cmd", "application/json", strings.NewReader(`{ "cam" : 1 }`)))
	raw, err := http.Get(s.URL + "/v1/focus/" + TestID + "/cmd")
	if err != nil {
		t.Fatal(err)
	}
	text, _ := io.ReadAll(raw.Body)
	raw.Body.Close()
	if string(text) != `{"cam":1}` {
		t.Errorf("a spaced command reached the board as %q, want {\"cam\":1}", text)
	}
	r.Body.Close() // the app gone: after focusIdleMax the board is told to stop
	time.Sleep(600 * time.Millisecond)
	if m := poll(); m["stop"] != true {
		t.Errorf("no viewer: %v, want stop", m)
	}
}

// a camera that sees nothing ("dark") is no leak and is told once; an ignored camera's change is no leak and its
// problems are not told; used again, told again (DESIGN.md 5.3.18)
func TestDarkAndIgnoredCamera(t *testing.T) {
	s := testServer(t)
	put := func(cam, body string) int {
		req, _ := http.NewRequest("PUT", s.URL+"/v1/devices/"+TestID+"/cameras/"+cam, strings.NewReader(body))
		r, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		r.Body.Close()
		return r.StatusCode
	}
	job := func(j string) map[string]any {
		ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "job": {j}, "jago": {"5"}})
		return ans
	}
	open := func() bool {
		mu.Lock()
		defer mu.Unlock()
		_, o := leaks[TestID]
		return o
	}
	flagged := func() [Cameras]string {
		mu.Lock()
		defer mu.Unlock()
		return focusTold[TestID]
	}
	ans := job("dark,same,-1,100")
	if open() {
		t.Fatal("a dark camera opened a leak")
	}
	if flagged() != ([Cameras]string{"dark", ""}) || ans["checkin"] != float64(FocusModeS) {
		t.Errorf("a dark camera: told %v, check-in %v; want dark, %d s", flagged(), ans["checkin"], FocusModeS)
	}
	if put("2", `{"ignore":true}`) != http.StatusBadRequest || put("0", `{}`) != http.StatusBadRequest {
		t.Error("camera 2 (of 0, 1) or no ignore: not refused")
	}
	if put("0", `{"ignore":true}`) != http.StatusOK {
		t.Fatal("ignore camera 0")
	}
	ans = job("changed,same,-1,100")
	if open() || flagged() != ([Cameras]string{}) || ans["checkin"] == float64(FocusModeS) {
		t.Errorf("an ignored camera: leak %v, told %v, check-in %v", open(), flagged(), ans["checkin"])
	}
	loadThresholds() // kept over a restart
	if !deviceThresholds(TestID).Ignore[0] {
		t.Error("ignore not kept in the file")
	}
	if put("0", `{"ignore":false}`) != http.StatusOK {
		t.Fatal("use camera 0 again")
	}
	job("dark,same,-1,100")
	if flagged() != ([Cameras]string{"dark", ""}) {
		t.Errorf("used again: told %v, want dark again", flagged())
	}
	job("same,changed,100,100")
	if !open() {
		t.Error("the other camera's change opened no leak")
	}
}
