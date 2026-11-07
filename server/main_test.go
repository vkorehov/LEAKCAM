package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strconv"
	"strings"
	"testing"
	"time"
)

const TestID = "7C3E8262B149"

// testServer: the routes under test behind withTime, the history and images in a temporary folder
func testServer(t *testing.T) *httptest.Server {
	t.Helper()
	dir := t.TempDir()
	metricFile, imageDir = dir+"/metrics.jsonl", dir
	checkinS, leakCheckS = CheckinS, LeakCheckS
	intervals, seen, phones = map[string]Intervals{}, map[string]CheckIn{}, map[string][]string{}
	leaks, wifiChanges, tokens, tokenFile = map[string]Leak{}, map[string]*WifiChange{}, map[string]string{}, dir+"/tokens.json"
	captures = map[string]string{}
	mux := http.NewServeMux()
	mux.HandleFunc("POST /v1/poll", poll)
	mux.HandleFunc("GET /v1/devices/{id}", device)
	mux.HandleFunc("PUT /v1/devices/{id}/intervals", setIntervals)
	mux.HandleFunc("POST /v1/devices/{id}/capture", capture)
	mux.HandleFunc("POST /v1/devices/{id}/clear", clearLeak)
	mux.HandleFunc("PUT /v1/devices/{id}/wifi", putWifi)
	s := httptest.NewServer(withTime(mux))
	t.Cleanup(s.Close)
	return s
}

func checkIn(t *testing.T, s *httptest.Server, form url.Values) (map[string]any, *http.Response) {
	t.Helper()
	r, err := http.PostForm(s.URL+"/v1/poll?id="+TestID, form)
	if err != nil {
		t.Fatal(err)
	}
	defer r.Body.Close()
	var ans map[string]any
	if err := json.NewDecoder(r.Body).Decode(&ans); err != nil {
		t.Fatalf("check-in answer: %v", err)
	}
	return ans, r
}

func put(t *testing.T, s *httptest.Server, body string) int {
	t.Helper()
	req, _ := http.NewRequest(http.MethodPut, s.URL+"/v1/devices/"+TestID+"/intervals", strings.NewReader(body))
	r, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	r.Body.Close()
	return r.StatusCode
}

// the check-in answer carries both intervals, the defaults first (DESIGN.md 5.3.12)
func TestCheckinAnswerDefaults(t *testing.T) {
	s := testServer(t)
	ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}})
	if ans["checkin"] != float64(CheckinS) || ans["leakcheck"] != float64(LeakCheckS) {
		t.Fatalf("answer %v: want checkin %d, leakcheck %d", ans, CheckinS, LeakCheckS)
	}
	if _, ok := ans["poll"]; ok {
		t.Fatalf("answer %v: the old key poll is gone", ans)
	}
}

// per device at runtime, 0 back to the default, out of range refused
func TestIntervalsPerDevice(t *testing.T) {
	s := testServer(t)
	if c := put(t, s, `{"checkin":60,"leakcheck":120}`); c != http.StatusOK {
		t.Fatalf("PUT: %d", c)
	}
	ans, _ := checkIn(t, s, url.Values{})
	if ans["checkin"] != float64(60) || ans["leakcheck"] != float64(120) {
		t.Fatalf("after PUT: %v", ans)
	}
	r, err := http.Get(s.URL + "/v1/devices/" + TestID)
	if err != nil {
		t.Fatal(err)
	}
	var d Device
	json.NewDecoder(r.Body).Decode(&d)
	r.Body.Close()
	if d.Intervals.Checkin != 60 || d.Intervals.LeakCheck != 120 || !d.Online {
		t.Fatalf("device view: %+v", d)
	}
	if c := put(t, s, `{"checkin":0,"leakcheck":120}`); c != http.StatusOK {
		t.Fatalf("PUT 0: %d", c)
	}
	if ans, _ = checkIn(t, s, url.Values{}); ans["checkin"] != float64(CheckinS) {
		t.Fatalf("0 = the default: %v", ans)
	}
	for _, bad := range []string{`{"checkin":4}`, `{"checkin":3601}`, `{"leakcheck":59}`, `{"leakcheck":604801}`, `x`} {
		if c := put(t, s, bad); c != http.StatusBadRequest {
			t.Errorf("PUT %s: %d, want 400", bad, c)
		}
	}
	other := deviceIntervals("0123456789AB")
	if other.Checkin != CheckinS || other.LeakCheck != LeakCheckS {
		t.Fatalf("another device keeps the defaults: %+v", other)
	}
}

// every answer carries X-Time, errors too (the board's clock from any exchange)
func TestXTimeOnEveryAnswer(t *testing.T) {
	s := testServer(t)
	_, r := checkIn(t, s, url.Values{})
	for _, resp := range []*http.Response{r, must(http.Get(s.URL + "/v1/devices/bad")), must(http.Get(s.URL + "/nothing"))} {
		v, err := strconv.ParseInt(resp.Header.Get("X-Time"), 10, 64)
		if err != nil || v < time.Now().Unix()-5 || v > time.Now().Unix()+5 {
			t.Errorf("%s %d: X-Time %q", resp.Request.URL.Path, resp.StatusCode, resp.Header.Get("X-Time"))
		}
	}
}

func must(r *http.Response, err error) *http.Response {
	if err != nil {
		panic(err)
	}
	r.Body.Close()
	return r
}

// the leak check's result is kept with the metrics (job, jago)
func TestJobMetrics(t *testing.T) {
	s := testServer(t)
	checkIn(t, s, url.Values{"job": {"changed,same"}, "jago": {"3"}, "probe": {"1650"}})
	mu.Lock()
	m := seen[TestID].Metrics
	mu.Unlock()
	if m["job"] != "changed,same" || m["jago"] != int64(3) || m["probe"] != int64(1650) {
		t.Fatalf("metrics %v", m)
	}
}

// online is judged by the interval the device was told at its last check-in: a shortened one is not yet in force
// (test111 2026-11-07: checkin 10 set on a board on 60 s showed it offline 30 s after its check-in)
func TestOnlineByToldInterval(t *testing.T) {
	s := testServer(t)
	checkIn(t, s, url.Values{"full": {"0.6+g1234567"}})
	mu.Lock()
	c := seen[TestID]
	c.At = time.Now().Add(-40 * time.Second) // 40 s ago, told 3600
	seen[TestID] = c
	mu.Unlock()
	if put(t, s, `{"checkin":10}`) != http.StatusOK {
		t.Fatal("PUT")
	}
	online := func() bool {
		r, err := http.Get(s.URL + "/v1/devices/" + TestID)
		if err != nil {
			t.Fatal(err)
		}
		defer r.Body.Close()
		var d Device
		if err := json.NewDecoder(r.Body).Decode(&d); err != nil {
			t.Fatal(err)
		}
		return d.Online
	}
	if !online() {
		t.Error("offline 40 s after a check-in told 3600 s, by the new 10 s not yet sent")
	}
	checkIn(t, s, url.Values{"full": {"0.6+g1234567"}}) // now told 10
	mu.Lock()
	c = seen[TestID]
	c.At = time.Now().Add(-40 * time.Second)
	seen[TestID] = c
	mu.Unlock()
	if online() {
		t.Error("online 40 s after a check-in told 10 s")
	}
}

// a photo or video asked for is handed over once, at the next check-in (DESIGN.md 5.3.14); a video 15 s
func TestCaptureAtNextCheckin(t *testing.T) {
	s := testServer(t)
	r := must(http.Post(s.URL+"/v1/devices/"+TestID+"/capture", "application/json", strings.NewReader(`{"kind":"video"}`)))
	if r.StatusCode != http.StatusOK {
		t.Fatal(r.Status)
	}
	ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}})
	c, _ := ans["capture"].(map[string]any)
	if c["kind"] != "video" || c["secs"] != float64(VideoS) {
		t.Errorf("capture in the answer: %v", ans["capture"])
	}
	if ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}}); ans["capture"] != nil {
		t.Errorf("handed over twice: %v", ans["capture"])
	}
}

// a wet probe opens a leak (the 5-min mode in the answer), the all-clear ends it; a pending network is delivered once
// and its passphrase forgotten; a failed change puts the board in the 5-min mode
func TestLeakAndWifiChange(t *testing.T) {
	s := testServer(t)
	ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "probe": {"800"}})
	if ans["mode"] != float64(1) || ans["checkin"] != float64(WakeModeS) {
		t.Errorf("leak: mode %v checkin %v", ans["mode"], ans["checkin"])
	}
	if c, _ := ans["capture"].(map[string]any); c["kind"] != "leak" || c["photos"] != float64(LeakPhotos) ||
		c["secs"] != float64(VideoS) {
		t.Errorf("a leak's record: %v", ans["capture"])
	}
	if c, _ := ans["capture"].(map[string]any); len(c["ids"].([]any)) != LeakPhotos+1 {
		t.Errorf("a leak's record IDs: %v", c["ids"])
	}
	must(http.Post(s.URL+"/v1/devices/"+TestID+"/clear", "", nil))
	if ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}}); ans["mode"] != float64(0) {
		t.Errorf("after the all-clear: mode %v", ans["mode"])
	}
	req, _ := http.NewRequest(http.MethodPut, s.URL+"/v1/devices/"+TestID+"/wifi", strings.NewReader(`{"ssid":"new","psk":"secret123"}`))
	must(http.DefaultClient.Do(req))
	ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}})
	if w, _ := ans["wifi"].(map[string]any); w["ssid"] != "new" || w["psk"] != "secret123" {
		t.Errorf("pending network: %v", ans["wifi"])
	}
	mu.Lock()
	psk := wifiChanges[TestID].PSK
	mu.Unlock()
	if psk != "" {
		t.Error("the passphrase kept after delivery")
	}
	if ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "wchg": {"17"}}); ans["mode"] != float64(1) {
		t.Errorf("a failed change: mode %v", ans["mode"])
	}
}
