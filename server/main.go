// LEAKCAM server, presence only (use case 2): each device checks in with its chip ID (GET /v1/poll?id=), the app asks
// whether a device is online (GET /v1/devices/{id}) and can wake its BLE (POST /v1/devices/{id}/wake: the next check-in
// answers "ble":1, the board advertises for 2 minutes, tests/test119), and keeps the boards' crash records (POST
// /v1/devices/{id}/crash, sent at a board's first API contact after a crash, DESIGN.md 5.3.3, tests/test128; listed by GET
// /v1/devices/{id}/crashes). Presence is in memory (after a restart the devices check in again); crash records are kept
// on disk (LEAKCAM_CRASHES, one JSON line each): the board clears its own copy once the server answered, so a restart
// must not lose one. The server is the one source of truth for who uses which board (DESIGN.md 5.3.5): a phone is
// registered as a board's user when it is issued the board's pairing PIN (GET /v1/devices/{id}/pin?phone=), removed by
// DELETE /v1/devices/{id}/phones/{phone}; clients list a phone's boards with GET /v1/phones/{phone}/devices and keep no
// list of their own. A board whose last phone left gets 410 Gone at its check-in: it goes back to the setup firmware.
// The users are kept on disk (LEAKCAM_PHONES, one JSON object). Firmware upgrades (DESIGN.md 5.3.7, tests/test130): a
// board reports its running image and version at each check-in (full= or setup=); when LEAKCAM_IMAGES holds a
// bl616_full.bin of another version (with its bl616_full.xz, the SDK build's), a full firmware's check-in answer names
// it with the SHA-256 and size of both; the board fetches the .xz from GET /v1/images/bl616_full into the free end of
// its slot, and the setup firmware unpacks it over the old image (no network needed from there).
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
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	PollS   = 60 // the check-in interval the server gives the devices (LEAKCAM_POLL_S: shorter for tests)
	Addr    = ":8090"
	Crashes = "/home/pi/leakcam-server/crashes.jsonl" // LEAKCAM_CRASHES
	WakeGap = 2 * time.Minute                         // at most one BLE wake per device this often: a wake costs its battery 2 minutes awake
	Phones  = "/home/pi/leakcam-server/phones.json"   // LEAKCAM_PHONES
	Images  = "/home/pi/leakcam-server/images"        // LEAKCAM_IMAGES: bl616_full.bin + .xz, the version boards get
)

type Device struct {
	ID       string `json:"id"`
	Online   bool   `json:"online"`
	LastSeen int64  `json:"last_seen_s"` // seconds since the last check-in, -1 if never seen
	SSID     string `json:"ssid"`        // the network it checked in from (not secret; the passphrase never comes)
	Wake     bool   `json:"wake"`        // a BLE wake waits for its next check-in
	Phones   int    `json:"phones"`      // its registered users (phones)
}

// Crash is one board crash record, as the board's `crash` prints it (tools/power-bringup/crash_decode.py reads it)
type Crash struct {
	Device string `json:"device"`
	Build  string `json:"build"` // "<image> <version>": the ELF to decode with (~/.cache/leakcam-elf)
	Count  int    `json:"count"`
	At     string `json:"received"`
	Record string `json:"record"`
}

type CheckIn struct {
	At      time.Time
	SSID    string
	Image   string // the running firmware: "full" or "setup"
	Version string
}

// Image is an upgrade on offer: its version read from its own build tag, so the file is all there is to update
type Image struct {
	Version string
	SHA     string // of the image (.bin): what the board must hold in the end
	Size    int64
	XzSHA   string // of the download (.xz)
	XzSize  int64
	mod     time.Time
}

var (
	idRe      = regexp.MustCompile(`^[0-9A-F]{12}$`) // the BL616 chip ID: its factory MAC, 12 upper-case hex digits
	mu        sync.Mutex
	seen      = map[string]CheckIn{}
	wake      = map[string]bool{}      // asked, not yet delivered
	woken     = map[string]time.Time{} // the last wake delivered
	pollS     = PollS
	pinKey    []byte                 // the BLE pairing PIN's key, shared with the firmware (LEAKCAM_PIN_KEY); never in the app
	crashes   = map[string][]Crash{} // per device; a resend (the same record text) is kept once
	crashFile = Crashes
	hexRe     = regexp.MustCompile(`^[0-9a-f]{1,8}$`)
	phoneRe   = regexp.MustCompile(`^[0-9a-f]{16}$`) // a phone: its Android ID (Settings.Secure.ANDROID_ID)
	// per device its users; a device whose last phone left stays with an empty list (410 at its check-in), one never
	// registered is absent (boards set up before the users were kept: no 410)
	phones    = map[string][]string{}
	phoneFile = Phones
	imageDir  = Images
	imageMeta = map[string]Image{} // by file, reread when it changes
	tagRe     = regexp.MustCompile(`leakcam-build ([a-z_]+) ([0-9A-Za-z.+-]+)`)
	verRe     = regexp.MustCompile(`^[0-9A-Za-z.+-]{1,48}$`)
)

// image: the upgrade imageDir/<name>.bin (its version from its build tag, for a firmware called want: "full") with its
// compressed download <name>.xz, or false (none, or not that image)
func image(name, want string) (Image, bool) {
	path := imageDir + "/" + name
	st, err1 := os.Stat(path + ".bin")
	sx, err2 := os.Stat(path + ".xz")
	if err1 != nil || err2 != nil {
		return Image{}, false
	}
	mod := st.ModTime()
	if sx.ModTime().After(mod) {
		mod = sx.ModTime()
	}
	mu.Lock()
	im, ok := imageMeta[name]
	mu.Unlock()
	if ok && im.mod.Equal(mod) && im.Size == st.Size() && im.XzSize == sx.Size() {
		return im, im.Version != ""
	}
	b, err1 := os.ReadFile(path + ".bin")
	x, err2 := os.ReadFile(path + ".xz")
	if err1 != nil || err2 != nil {
		log.Printf("image %s: %v %v", path, err1, err2)
		return Image{}, false
	}
	im = Image{SHA: fmt.Sprintf("%x", sha256.Sum256(b)), Size: int64(len(b)), XzSHA: fmt.Sprintf("%x", sha256.Sum256(x)),
		XzSize: int64(len(x)), mod: mod}
	if m := tagRe.FindSubmatch(b); m != nil && string(m[1]) == want {
		im.Version = string(m[2])
	} else {
		log.Printf("image %s.bin: no leakcam-build %s tag: not offered", path, want)
	}
	mu.Lock()
	imageMeta[name] = im
	mu.Unlock()
	return im, im.Version != ""
}

// CrashKinds as the firmware names them (crash_rec.c kind_name)
var CrashKinds = []string{"?", "exception", "watchdog hang", "assert", "stack overflow", "malloc failed", "fatal", "k230 fatal"}

func reply(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(v); err != nil {
		log.Printf("reply: %v", err)
	}
}

func poll(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	id := q.Get("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	in := CheckIn{At: time.Now(), SSID: q.Get("ssid")}
	for _, img := range []string{"full", "setup"} {
		if v := q.Get(img); verRe.MatchString(v) {
			in.Image, in.Version = img, v
		}
	}
	mu.Lock()
	seen[id] = in
	if p, ok := phones[id]; ok && len(p) == 0 {
		mu.Unlock()
		http.Error(w, "no user: back to setup", http.StatusGone)
		return
	}
	ble := 0
	if wake[id] {
		ble = 1
		delete(wake, id)
		woken[id] = time.Now()
	}
	mu.Unlock()
	ans := map[string]any{"stream": 0, "poll": pollS, "ble": ble}
	// an upgrade only to a full firmware that said its version: the setup firmware is changed by wire only
	if in.Image == "full" {
		if im, ok := image("bl616_full", "full"); ok && im.Version != in.Version {
			ans["full"], ans["full_sha"], ans["full_size"] = im.Version, im.SHA, im.Size
			ans["xz_sha"], ans["xz_size"] = im.XzSHA, im.XzSize
			log.Printf("%s: full %s -> %s offered", id, in.Version, im.Version)
		}
	}
	reply(w, ans)
}

// images: the upgrade's download, the .xz (the board checks its SHA-256 against the check-in answer's)
func images(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("name")
	if name != "bl616_full" {
		http.NotFound(w, r)
		return
	}
	if _, ok := image(name, "full"); !ok {
		http.NotFound(w, r)
		return
	}
	http.ServeFile(w, r, imageDir+"/"+name+".xz")
}

// crash: a board's crash record (firmware/bl616_common/api.c api_crash_target), answered 200 once kept: only then does the
// board clear it. Kept once per (device, build, count)
func crash(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	if err := r.ParseForm(); err != nil {
		http.Error(w, "form", http.StatusBadRequest)
		return
	}
	q := r.PostForm
	hx := map[string]uint64{}
	for _, k := range []string{"c", "pc", "v", "ra", "sp"} {
		if !hexRe.MatchString(q.Get(k)) {
			http.Error(w, k+": hex", http.StatusBadRequest)
			return
		}
		hx[k], _ = strconv.ParseUint(q.Get(k), 16, 32)
	}
	n, err1 := strconv.Atoi(q.Get("n"))
	kind, err2 := strconv.Atoi(q.Get("k"))
	up, err3 := strconv.Atoi(q.Get("up"))
	st, err4 := strconv.Atoi(q.Get("st"))
	if err1 != nil || err2 != nil || err3 != nil || err4 != nil || n < 1 || q.Get("image") == "" || q.Get("ver") == "" {
		http.Error(w, "n, k, up, st, image, ver", http.StatusBadRequest)
		return
	}
	if kind < 0 || kind >= len(CrashKinds) {
		kind = 0
	}
	bt := ""
	for _, a := range strings.Split(q.Get("bt"), ",") {
		if hexRe.MatchString(a) {
			v, _ := strconv.ParseUint(a, 16, 32)
			bt += fmt.Sprintf(" 0x%08x", v)
		}
	}
	build := q.Get("image") + " " + q.Get("ver")
	rec := fmt.Sprintf("crash #%d (%s): %s in task '%s' at %d ms; mcause 0x%08x mepc 0x%08x mtval 0x%08x ra 0x%08x sp 0x%08x\n",
		n, build, CrashKinds[kind], q.Get("t"), up, hx["c"], hx["pc"], hx["v"], hx["ra"], hx["sp"])
	if st != 9 {
		rec += fmt.Sprintf("crash record incomplete: the recorder stopped at stage %d\n", st)
	}
	if m := q.Get("m"); m != "" { // a fatal error's own words (crash_rec_fatal, crash_rec_k230)
		rec += fmt.Sprintf("crash message: %s\n", m)
	}
	rec += "crash backtrace:" + bt + "\n"
	mu.Lock()
	defer mu.Unlock()
	// the whole text, not (build, count): the count starts at 1 again after every send, so two crashes of one build
	// can share it; a resend after a lost answer is the same text
	for _, c := range crashes[id] {
		if c.Record == rec {
			reply(w, map[string]bool{"kept": true})
			return
		}
	}
	c := Crash{id, build, n, time.Now().UTC().Format(time.RFC3339), rec}
	if err := appendCrash(c); err != nil {
		log.Printf("crash from %s not stored: %v", id, err)
		http.Error(w, "not stored", http.StatusInternalServerError) // the board keeps it and sends it again
		return
	}
	crashes[id] = append(crashes[id], c)
	log.Printf("crash from %s:\n%s", id, rec)
	reply(w, map[string]bool{"kept": true})
}

// appendCrash writes one record to the file, synced, before the board hears "kept"
func appendCrash(c Crash) error {
	f, err := os.OpenFile(crashFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o600)
	if err != nil {
		return err
	}
	b, err := json.Marshal(c)
	if err == nil {
		_, err = f.Write(append(b, '\n'))
	}
	if err == nil {
		err = f.Sync()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	return err
}

func loadCrashes() {
	b, err := os.ReadFile(crashFile)
	if os.IsNotExist(err) {
		return
	}
	if err != nil {
		log.Fatalf("crash records %s: %v", crashFile, err)
	}
	for _, l := range strings.Split(string(b), "\n") {
		var c Crash
		if l == "" {
			continue
		}
		if err := json.Unmarshal([]byte(l), &c); err != nil {
			log.Fatalf("crash records %s: %v", crashFile, err)
		}
		crashes[c.Device] = append(crashes[c.Device], c)
	}
}

func deviceCrashes(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	c := append([]Crash{}, crashes[id]...)
	mu.Unlock()
	reply(w, c)
}

// deviceInfo: a device's state for a client; mu held
func deviceInfo(id string) Device {
	c, ok := seen[id]
	d := Device{ID: id, LastSeen: -1, Wake: wake[id], Phones: len(phones[id])}
	if ok {
		d.LastSeen = int64(time.Since(c.At).Seconds())
		d.SSID = c.SSID
		d.Online = d.LastSeen < int64(3*pollS) // three check-ins missed: offline
	}
	return d
}

func device(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if !idRe.MatchString(id) {
		http.Error(w, "id: 12 upper-case hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	d := deviceInfo(id)
	mu.Unlock()
	reply(w, d)
}

// savePhones writes the users to the file (a temporary file renamed over it, synced); mu held
func savePhones() error {
	b, err := json.Marshal(phones)
	if err != nil {
		return err
	}
	tmp := phoneFile + ".tmp"
	f, err := os.OpenFile(tmp, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o600)
	if err != nil {
		return err
	}
	_, err = f.Write(b)
	if err == nil {
		err = f.Sync()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(tmp, phoneFile)
	}
	return err
}

func loadPhones() {
	b, err := os.ReadFile(phoneFile)
	if os.IsNotExist(err) {
		return
	}
	if err == nil {
		err = json.Unmarshal(b, &phones)
	}
	if err != nil {
		log.Fatalf("users %s: %v", phoneFile, err)
	}
}

// register: the phone becomes a user of the device (the PIN was issued to it); mu held
func register(id, phone string) error {
	for _, p := range phones[id] {
		if p == phone {
			return nil
		}
	}
	phones[id] = append(phones[id], phone)
	if err := savePhones(); err != nil {
		phones[id] = phones[id][:len(phones[id])-1]
		return err
	}
	return nil
}

// unregister: DELETE /v1/devices/{id}/phones/{phone}, the phone no longer a user (the app's delete); the board's next
// check-in gets 410 when no phone is left
func unregister(w http.ResponseWriter, r *http.Request) {
	id, phone := r.PathValue("id"), r.PathValue("phone")
	if !idRe.MatchString(id) || !phoneRe.MatchString(phone) {
		http.Error(w, "id: 12 upper-case hex digits, phone: 16 hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	defer mu.Unlock()
	old, kept := phones[id], []string{}
	for _, p := range old {
		if p != phone {
			kept = append(kept, p)
		}
	}
	if len(kept) == len(old) {
		http.Error(w, "not a user of this device", http.StatusNotFound)
		return
	}
	phones[id] = kept
	if err := savePhones(); err != nil {
		phones[id] = old
		log.Printf("users not stored: %v", err)
		http.Error(w, "not stored", http.StatusInternalServerError)
		return
	}
	reply(w, map[string]int{"phones": len(kept)})
}

// phoneDevices: GET /v1/phones/{phone}/devices, the boards of a phone with their state: the client's list
func phoneDevices(w http.ResponseWriter, r *http.Request) {
	phone := r.PathValue("phone")
	if !phoneRe.MatchString(phone) {
		http.Error(w, "phone: 16 hex digits", http.StatusBadRequest)
		return
	}
	mu.Lock()
	list := []Device{}
	for id, ps := range phones {
		for _, p := range ps {
			if p == phone {
				list = append(list, deviceInfo(id))
			}
		}
	}
	mu.Unlock()
	sort.Slice(list, func(i, j int) bool { return list[i].ID < list[j].ID })
	reply(w, list)
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
// firmware/bl616_setup/ble_pairing.c derives it; the app asks for it right before pairing. Open on the LAN until the
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
	// the phone it is issued to becomes a user of the board (every setup asks for the PIN)
	if phone := r.URL.Query().Get("phone"); phone != "" {
		if !phoneRe.MatchString(phone) {
			http.Error(w, "phone: 16 hex digits", http.StatusBadRequest)
			return
		}
		mu.Lock()
		err := register(id, phone)
		mu.Unlock()
		if err != nil {
			log.Printf("user %s of %s not stored: %v", phone, id, err)
			http.Error(w, "not stored", http.StatusInternalServerError)
			return
		}
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
	if f := os.Getenv("LEAKCAM_CRASHES"); f != "" {
		crashFile = f
	}
	loadCrashes()
	if f := os.Getenv("LEAKCAM_PHONES"); f != "" {
		phoneFile = f
	}
	loadPhones()
	if d := os.Getenv("LEAKCAM_IMAGES"); d != "" {
		imageDir = d
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
	http.HandleFunc("POST /v1/devices/{id}/crash", crash)
	http.HandleFunc("GET /v1/devices/{id}/crashes", deviceCrashes)
	http.HandleFunc("POST /v1/devices/{id}/wake", wakeUp)
	http.HandleFunc("DELETE /v1/devices/{id}/phones/{phone}", unregister)
	http.HandleFunc("GET /v1/phones/{phone}/devices", phoneDevices)
	http.HandleFunc("GET /v1/images/{name}", images)
	log.Printf("listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, nil))
}
