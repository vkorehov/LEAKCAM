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
	mux := http.NewServeMux()
	mux.HandleFunc("POST /v1/poll", poll)
	mux.HandleFunc("GET /v1/devices/{id}", device)
	mux.HandleFunc("PUT /v1/devices/{id}/intervals", setIntervals)
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
