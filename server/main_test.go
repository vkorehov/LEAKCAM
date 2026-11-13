package main

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"slices"
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
	metricFile, imageDir, mediaDir = dir+"/metrics.jsonl", dir, dir
	checkinS, leakCheckS = CheckinS, LeakCheckS
	intervals, seen, phones = map[string]Intervals{}, map[string]CheckIn{}, map[string][]string{}
	thresholds, thrFile = map[string]Thresholds{}, dir+"/thresholds.json"
	leaks, wifiChanges, tokens, tokenFile = map[string]Leak{}, map[string]*WifiChange{}, map[string]string{}, dir+"/tokens.json"
	captures, notLeak, clearedAt = map[string][]Capture{}, map[string]string{}, map[string]time.Time{}
	mux := http.NewServeMux()
	mux.HandleFunc("POST /v1/poll", poll)
	mux.HandleFunc("GET /v1/devices/{id}", device)
	mux.HandleFunc("PUT /v1/devices/{id}/intervals", setIntervals)
	mux.HandleFunc("PUT /v1/devices/{id}/thresholds", setThresholds)
	mux.HandleFunc("PUT /v1/devices/{id}/cameras/{cam}", setIgnore)
	mux.HandleFunc("POST /v1/devices/{id}/capture", capture)
	mux.HandleFunc("POST /v1/devices/{id}/clear", clearLeak)
	mux.HandleFunc("PUT /v1/devices/{id}/wifi", putWifi)
	mux.HandleFunc("POST /v1/media/{id}", putMedia)
	mux.HandleFunc("GET /v1/live/{id}/{file}", liveVideo)
	mux.HandleFunc("GET /v1/media/{id}", media)
	mux.HandleFunc("POST /v1/devices/{id}/focus", askFocus)
	mux.HandleFunc("POST /v1/focus/{id}/stream", focusStream)
	mux.HandleFunc("GET /v1/focus/{id}/live", focusLive)
	mux.HandleFunc("POST /v1/focus/{id}/cmd", focusCmd)
	mux.HandleFunc("GET /v1/focus/{id}/cmd", focusPoll)
	ignoreSent, askedAt, focusEnded = map[string]time.Time{}, map[string]time.Time{}, map[string]time.Time{}
	focuses, focusAsked, focusTold, focusSince = map[string]*focusSession{}, map[string]time.Time{},
		map[string][Cameras]string{}, map[string]time.Time{}
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
	for _, bad := range []string{`{"checkin":4}`, `{"checkin":3601}`, `{"leakcheck":9}`, `{"leakcheck":604801}`, `x`} {
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
	// one of each kind waits: a second video ask adds nothing, a photo joins the queue
	must(http.Post(s.URL+"/v1/devices/"+TestID+"/capture", "application/json", strings.NewReader(`{"kind":"video"}`)))
	must(http.Post(s.URL+"/v1/devices/"+TestID+"/capture", "application/json", strings.NewReader(`{"kind":"photo"}`)))
	mu.Lock()
	p := pending(TestID)
	mu.Unlock()
	if len(p) != 2 || p[0].Kind != "photo" || p[1].Kind != "video" || p[0].State != "waiting" {
		t.Fatalf("pending: %+v", p)
	}
	// a Wi-Fi change first (user 2026-11-07), then the photo, then the video: one per check-in
	req, _ := http.NewRequest(http.MethodPut, s.URL+"/v1/devices/"+TestID+"/wifi", strings.NewReader(`{"ssid":"new","psk":"secret123"}`))
	must(http.DefaultClient.Do(req))
	ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}})
	if ans["wifi"] == nil || ans["capture"] != nil {
		t.Errorf("the network first, alone: %v %v", ans["wifi"], ans["capture"])
	}
	if ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}}); ans["capture"].(map[string]any)["kind"] != "photo" {
		t.Errorf("the photo next: %v", ans["capture"])
	}
	// the video still waits: the board checks in again right after the photo's SENT
	if m := ans["capture"].(map[string]any)["more"]; m != float64(1) {
		t.Errorf("the photo's more: %v, want 1", m)
	}
	// one picture per camera, then one video per camera (user 2026-11-12)
	if got := idEnds(ans["capture"]); got != "p1,p2" {
		t.Errorf("the photo's IDs: %s", got)
	}
	ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}})
	c, _ := ans["capture"].(map[string]any)
	if c["kind"] != "video" || c["secs"] != float64(VideoS) || c["cams"] != float64(Cameras) {
		t.Errorf("then the video: %v", ans["capture"])
	}
	if got := idEnds(ans["capture"]); got != "v1,v2" {
		t.Errorf("the video's IDs: %s", got)
	}
	if c["more"] != float64(0) {
		t.Errorf("the video's more: %v, want 0", c["more"])
	}
	if ans, _ = checkIn(t, s, url.Values{"full": {"0.6+g1234567"}}); ans["capture"] != nil {
		t.Errorf("handed over twice: %v", ans["capture"])
	}
}

// a wet probe opens a leak (the 1-min leak mode in the answer), the all-clear ends it; a pending network is delivered once
// and its passphrase forgotten; a failed change puts the board in the 5-min mode
func TestLeakAndWifiChange(t *testing.T) {
	s := testServer(t)
	ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "probe": {"800"}})
	if ans["mode"] != float64(1) || ans["checkin"] != float64(LeakModeS) {
		t.Errorf("leak: mode %v checkin %v", ans["mode"], ans["checkin"])
	}
	if c, _ := ans["capture"].(map[string]any); c["kind"] != "leak" || c["photos"] != float64(LeakPhotos) ||
		c["secs"] != float64(VideoS) || c["gap_ms"] != float64(LeakGapMs) {
		t.Errorf("a leak's record: %v", ans["capture"])
	}
	if got := idEnds(ans["capture"]); got != "p1,p2,p3,p4,v1,v2" {
		t.Errorf("a leak's record IDs: %s", got)
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

// an uploaded picture is served under its ID; a bad ID is refused
func TestMediaUpload(t *testing.T) {
	s := testServer(t)
	id := TestID + "-1794081605-p2"
	req, _ := http.NewRequest(http.MethodPost, s.URL+"/v1/media/"+id, strings.NewReader("JPEGDATA"))
	req.Header.Set("X-Taken", "1794081607250") // the board's capture time, unix ms
	r := must(http.DefaultClient.Do(req))
	if r.StatusCode != http.StatusOK {
		t.Fatal(r.Status)
	}
	r, err := http.Get(s.URL + "/v1/media/" + id)
	if err != nil {
		t.Fatal(err)
	}
	b, _ := io.ReadAll(r.Body)
	r.Body.Close()
	if string(b) != "JPEGDATA" {
		t.Errorf("served %q %s", b, r.Status)
	}
	if got := r.Header.Get("X-Taken"); got != "1794081607250" {
		t.Errorf("X-Taken %q: the capture time is lost", got)
	}
	if r = must(http.Post(s.URL+"/v1/media/../x", "image/jpeg", strings.NewReader("x"))); r.StatusCode == http.StatusOK {
		t.Errorf("bad ID stored")
	}
}

// Not a leak re-arms the probe only once it has dried; the uploaded files are listed newest first
func TestNotALeak(t *testing.T) {
	s := testServer(t)
	checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "probe": {"800"}})
	must(http.Post(s.URL+"/v1/devices/"+TestID+"/clear?not_leak=1", "", nil))
	if ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "probe": {"800"}}); ans["mode"] != float64(0) {
		t.Errorf("still wet after Not a leak: mode %v", ans["mode"])
	}
	checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "probe": {"3000"}})
	if ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "probe": {"800"}}); ans["mode"] != float64(1) {
		t.Errorf("wet again after drying: mode %v", ans["mode"])
	}
	for _, id := range []string{"-1794081605-p1", "-1794081700-v1"} {
		must(http.Post(s.URL+"/v1/media/"+TestID+id, "", strings.NewReader("x")))
	}
	if m := deviceMedia(TestID); len(m) != 2 || m[0].Kind != "video" || m[1].At != 1794081605 {
		t.Errorf("media list: %+v", m)
	}
}

// 85 %RH or more at any reading opens a leak (DESIGN.md 5.3.15); 84.9 does not; Not a leak keeps it quiet until the
// humidity fell below 85 %RH; the room keys are kept
func TestHumidityLeak(t *testing.T) {
	s := testServer(t)
	if ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "rh": {"849"}}); ans["mode"] != float64(0) {
		t.Errorf("84.9 %%RH opened a leak: mode %v", ans["mode"])
	}
	if ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "rh": {"850"}}); ans["mode"] != float64(1) {
		t.Errorf("85.0 %%RH: no leak: mode %v", ans["mode"])
	}
	mu.Lock()
	l, open := leaks[TestID]
	mu.Unlock()
	if !open || l.Why != "humidity high" {
		t.Errorf("leak: %v %+v", open, l)
	}
	must(http.Post(s.URL+"/v1/devices/"+TestID+"/clear?not_leak=1", "", nil))
	if ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "rh": {"900"}}); ans["mode"] != float64(0) {
		t.Errorf("still humid after Not a leak: mode %v", ans["mode"])
	}
	checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "rh": {"600"}, "troom": {"212"}, "rhroom": {"583"},
		"roomago": {"40"}, "quiet": {"11000"}})
	mu.Lock()
	m := seen[TestID].Metrics
	mu.Unlock()
	if m["troom"] != int64(212) || m["rhroom"] != int64(583) || m["quiet"] != int64(11000) {
		t.Errorf("room keys: %v", m)
	}
	if ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "rh": {"860"}}); ans["mode"] != float64(1) {
		t.Errorf("humid again after it fell: mode %v", ans["mode"])
	}
}

func putThr(t *testing.T, s *httptest.Server, body string) int {
	t.Helper()
	req, _ := http.NewRequest(http.MethodPut, s.URL+"/v1/devices/"+TestID+"/thresholds", strings.NewReader(body))
	r, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	r.Body.Close()
	return r.StatusCode
}

// the leak thresholds ride every check-in answer (DESIGN.md 5.3.16): the defaults, per device, kept in the file across
// a restart, judged with the device's own values, out of range refused, 0 back to the default
func TestThresholds(t *testing.T) {
	s := testServer(t)
	full := url.Values{"full": {"0.6+g1234567"}}
	if ans, _ := checkIn(t, s, full); ans["probe_mv"] != float64(LeakProbeMV) || ans["humid"] != float64(HumidPct) {
		t.Fatalf("defaults: %v", ans)
	}
	for _, bad := range []string{`{"probe_mv":1125}`, `{"probe_mv":450}`, `{"probe_mv":1650}`, `{"humid":49}`, `{"humid":100}`} {
		if c := putThr(t, s, bad); c != http.StatusBadRequest {
			t.Errorf("%s: %d, want 400", bad, c)
		}
	}
	if c := putThr(t, s, `{"probe_mv":900,"humid":70}`); c != http.StatusOK {
		t.Fatalf("put: %d", c)
	}
	if ans, _ := checkIn(t, s, full); ans["probe_mv"] != float64(900) || ans["humid"] != float64(70) {
		t.Fatalf("per device: %v", ans)
	}
	mu.Lock()
	thresholds = map[string]Thresholds{} // a restart: read back from the file
	mu.Unlock()
	loadThresholds()
	if got := deviceThresholds(TestID); got != (Thresholds{ProbeMV: 900, Humid: 70}) {
		t.Fatalf("after a restart: %+v", got)
	}
	// judged with the device's values: 72 %RH is humid at 70, a probe at 1000 mV is dry at 900
	if ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "probe": {"1000"}, "rh": {"650"}}); ans["mode"] != float64(0) {
		t.Errorf("dry and below 70 %%RH: mode %v", ans["mode"])
	}
	if ans, _ := checkIn(t, s, url.Values{"full": {"0.6+g1234567"}, "rh": {"720"}, "thmv": {"900"}, "thrh": {"70"}}); ans["mode"] != float64(1) {
		t.Errorf("72 %%RH at a 70 threshold: mode %v", ans["mode"])
	}
	mu.Lock()
	m := seen[TestID].Metrics
	mu.Unlock()
	if m["thmv"] != int64(900) || m["thrh"] != int64(70) {
		t.Errorf("the thresholds in use not kept: %v", m)
	}
	if c := putThr(t, s, `{"probe_mv":0,"humid":0}`); c != http.StatusOK {
		t.Fatalf("put 0: %d", c)
	}
	if ans, _ := checkIn(t, s, full); ans["probe_mv"] != float64(LeakProbeMV) || ans["humid"] != float64(HumidPct) {
		t.Fatalf("back to the defaults: %v", ans)
	}
}

// idEnds: a capture answer's IDs without the "<device>-<unix s>-" prefix, comma-joined
func idEnds(capture any) string {
	c, _ := capture.(map[string]any)
	ids, _ := c["ids"].([]any)
	ends := make([]string, 0, len(ids))
	for _, x := range ids {
		s, _ := x.(string)
		ends = append(ends, s[strings.LastIndex(s, "-")+1:])
	}
	return strings.Join(ends, ",")
}

// a camera change opens a leak; after the all-clear the same job result (still riding on the check-ins for 60 s) does
// not open it again (test140, 2026-11-12), a newer leak check's change does
func TestCameraChangeAfterClear(t *testing.T) {
	s := testServer(t)
	form := func(jago string) url.Values {
		return url.Values{"full": {"0.6+g1234567"}, "job": {"changed,same"}, "jago": {jago}}
	}
	if ans, _ := checkIn(t, s, form("2")); ans["mode"] != float64(1) {
		t.Fatalf("a camera change: no leak: mode %v", ans["mode"])
	}
	time.Sleep(1100 * time.Millisecond) // the clear one second after the job
	must(http.Post(s.URL+"/v1/devices/"+TestID+"/clear", "", nil))
	mu.Lock()
	q := captures[TestID]
	mu.Unlock()
	if slices.ContainsFunc(q, func(c Capture) bool { return c.Kind == "leak" }) {
		t.Errorf("the cleared leak's record still waits: %+v", q)
	}
	if ans, _ := checkIn(t, s, form("5")); ans["mode"] != float64(0) {
		t.Errorf("the cleared job's result opened the leak again: mode %v", ans["mode"])
	}
	if ans, _ := checkIn(t, s, form("0")); ans["mode"] != float64(1) {
		t.Errorf("a new leak check's change after the clear: no leak: mode %v", ans["mode"])
	}
}

// a camera out of focus or never tuned (DESIGN.md 5.3.17): one "focus" message per camera and reason, again only after
// the camera was fine in between; an older board's job (no focus fields) and a missing camera say nothing
func TestFocusAlerts(t *testing.T) {
	testServer(t)
	mu.Lock()
	defer mu.Unlock()
	send := func(job string) (got []string) {
		for _, m := range focusAlerts(TestID, map[string]any{"job": job}, "home") {
			got = append(got, m["cam"]+":"+m["why"])
		}
		return got
	}
	for _, c := range []struct {
		job, want string
	}{
		{"same,same", ""},
		{"same,nocam,-1,-1", "0:untuned"},
		{"same,nocam,-1,-1", ""},
		{"same,same,100,98", ""},
		{"same,unfocused,95,41", "1:unfocused"},
		{"changed,unfocused,95,40", ""},
		{"same,same,96,100", ""},
		{"unfocused,unfocused,50,45", "0:unfocused 1:unfocused"},
	} {
		if got := strings.Join(send(c.job), " "); got != c.want {
			t.Errorf("job %s: %q, want %q", c.job, got, c.want)
		}
	}
}
