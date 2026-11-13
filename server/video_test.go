package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h264"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h265"
	mp4codecs "github.com/bluenviron/mediacommon/v2/pkg/formats/mp4/codecs"
	"github.com/bluenviron/mediacommon/v2/pkg/formats/pmp4"
)

// ok: the value, the test failed on an error
func ok(t *testing.T) func([]byte, error) []byte {
	return func(b []byte, err error) []byte {
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
}

// framed: testdata/clip.h265 (4 s, 15 fps, 320x240, an IDR every second; made with ffmpeg's libx265) in the board's
// framing: per access unit [u32 BE length][u64 BE pts us][Annex-B], at 15 fps
func framed(t *testing.T) ([]byte, int) {
	raw := ok(t)(os.ReadFile("testdata/clip.h265"))
	var nalus [][]byte // split at the start codes (the library caps one parse at 50)
	for _, part := range bytes.Split(raw, []byte{0, 0, 1}) {
		if part = bytes.TrimRight(part, "\x00"); len(part) > 0 {
			nalus = append(nalus, part)
		}
	}
	var out bytes.Buffer
	var au [][]byte
	frames, pics := 0, 0
	vclIn := false
	flush := func() {
		b := ok(t)(h264.AnnexB(au).Marshal())
		binary.Write(&out, binary.BigEndian, uint32(len(b)))
		binary.Write(&out, binary.BigEndian, uint64(1e9+int64(pics)*1e6/15)) // the camera's clock: any start
		out.Write(b)
		if vclIn { // a picture: the next one 1/15 s later (a header or SEI piece keeps the time)
			pics++
		}
		au = nil
		frames++
	}
	for _, n := range nalus {
		typ := h265.NALUType((n[0] >> 1) & 0x3f)
		vcl := typ < 32
		// a new picture's first slice, its parameter sets (sent as a frame of their own, as the K230's encoder does)
		if (vcl && n[2]&0x80 != 0 && vclIn) || (!vcl && vclIn) || (vcl && !vclIn && len(au) > 0) {
			flush()
			vclIn = false
		}
		au = append(au, n)
		vclIn = vclIn || vcl
	}
	flush()
	return out.Bytes(), frames
}

func TestVideoUpload(t *testing.T) {
	s := testServer(t)
	body, frames := framed(t)
	id := TestID + "-1794081605-v1"
	req, _ := http.NewRequest(http.MethodPost, s.URL+"/v1/media/"+id, bytes.NewReader(body))
	req.Header.Set("Content-Type", VideoType)
	req.Header.Set("X-Taken", "1794081607250")
	r, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	b, _ := io.ReadAll(r.Body)
	r.Body.Close()
	if r.StatusCode != http.StatusOK || !strings.Contains(string(b), `"frames":60`) {
		t.Fatalf("%s %s (%d frames sent)", r.Status, b, frames)
	}
	if r, err = http.Get(s.URL + "/v1/media/" + id); err != nil {
		t.Fatal(err)
	}
	mp4 := ok(t)(io.ReadAll(r.Body))
	r.Body.Close()
	if out := os.Getenv("LEAKCAM_MP4_OUT"); out != "" { // for a look with other tools (ffprobe)
		os.WriteFile(out, mp4, 0o644)
	}
	var p pmp4.Presentation
	if err := p.Unmarshal(bytes.NewReader(mp4)); err != nil {
		t.Fatal(err)
	}
	tr := p.Tracks[0]
	if len(tr.Samples) != 60 || tr.TimeScale != VideoClock {
		t.Errorf("%d samples, timescale %d", len(tr.Samples), tr.TimeScale)
	}
	syncs := 0
	for _, sa := range tr.Samples {
		if !sa.IsNonSyncSample {
			syncs++
		}
	}
	if syncs != 4 {
		t.Errorf("%d sync samples, want 4 (an IDR a second)", syncs)
	}
	i := bytes.Index(mp4, []byte("mvhd"))
	if got := time.Unix(int64(binary.BigEndian.Uint32(mp4[i+8:]))-2082844800, 0); got.Unix() != 1794081607 {
		t.Errorf("creation time %v", got)
	}
	if r.Header.Get("X-Taken") != "1794081607250" {
		t.Errorf("X-Taken %q", r.Header.Get("X-Taken"))
	}
	if r = must(http.Get(s.URL + "/v1/live/" + id + "/index.m3u8")); r.StatusCode != http.StatusNotFound {
		t.Errorf("live after the end: %s", r.Status)
	}
}

// the live view while the upload runs: the frames at the board's pace (15 fps), the HLS playlist answers before the
// last one has come (gohlslib serves an fMP4 playlist from its 2nd whole segment: ~2 s behind)
func TestVideoLive(t *testing.T) {
	t.Run("video", func(t *testing.T) { videoLive(t, false) })
	t.Run("with audio", func(t *testing.T) { videoLive(t, true) }) // the HLS muxer with an Opus track too
}

func videoLive(t *testing.T, audio bool) {
	s := testServer(t)
	body, _ := framed(t)
	if audio {
		body, _ = withAudio(t, body)
	}
	id := TestID + "-1794081605-v2"
	pr, pw := io.Pipe()
	req, _ := http.NewRequest(http.MethodPost, s.URL+"/v1/media/"+id, pr)
	req.Header.Set("Content-Type", VideoType)
	if audio {
		req.Header.Set("X-Audio", "opus/16000/1")
	}
	done := make(chan *http.Response, 1)
	go func() {
		r, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Error(err)
		}
		done <- r
	}()
	sent := make(chan time.Time, 1)
	go func() {
		for b := body; len(b) > 4; {
			l := binary.BigEndian.Uint32(b)
			n := 12 + int(l&^AudioFlag)
			pw.Write(b[:n])
			b = b[n:]
			if l&AudioFlag == 0 { // the pictures at the board's pace, the sound with them
				time.Sleep(time.Second / 15)
			}
		}
		sent <- time.Now()
		pw.Close()
	}()
	c := &http.Client{Timeout: 5 * time.Second}
	var got int
	var at time.Time
	for i := 0; i < 40 && got != http.StatusOK; i++ {
		if r, err := c.Get(s.URL + "/v1/live/" + id + "/index.m3u8"); err == nil {
			got, at = r.StatusCode, time.Now()
			r.Body.Close()
		}
		if got != http.StatusOK {
			time.Sleep(50 * time.Millisecond)
		}
	}
	end := <-sent
	if r := <-done; r == nil || r.StatusCode != http.StatusOK {
		t.Fatal("the upload failed")
	}
	if got != http.StatusOK || !at.Before(end) {
		t.Errorf("live playlist during the upload: %d at %v, the last frame at %v", got, at, end)
	}
}

// withAudio: the framed clip with the microphone's Opus packets put in by time, as the board sends them (the length's
// top bit; 40 ms each, from 20 ms after the first picture), and one packet from before the first picture (dropped)
func withAudio(t *testing.T, body []byte) ([]byte, int) {
	pkt := []byte{0xf8, 0xff, 0xfe} // a silent CELT packet: the server keeps the bytes, it does not decode them
	put := func(out *bytes.Buffer, us int64) {
		binary.Write(out, binary.BigEndian, uint32(len(pkt))|AudioFlag)
		binary.Write(out, binary.BigEndian, uint64(us))
		out.Write(pkt)
	}
	var out bytes.Buffer
	put(&out, 1e9-40000)
	next, n := int64(1e9+20000), 0
	for b := body; len(b) > 4; {
		l := 12 + int(binary.BigEndian.Uint32(b))
		pts := int64(binary.BigEndian.Uint64(b[4:]))
		for ; next <= pts; next += AudioPacketUs {
			put(&out, next)
			n++
		}
		out.Write(b[:l])
		b = b[l:]
	}
	return out.Bytes(), n
}

func TestVideoAudio(t *testing.T) {
	s := testServer(t)
	body, _ := framed(t)
	body, packets := withAudio(t, body)
	id := TestID + "-1794081605-v1"
	post := func(audio bool) (*http.Response, []byte) {
		mid := id
		if !audio {
			mid = TestID + "-1794081605-v2"
		}
		req, _ := http.NewRequest(http.MethodPost, s.URL+"/v1/media/"+mid, bytes.NewReader(body))
		req.Header.Set("Content-Type", VideoType)
		if audio {
			req.Header.Set("X-Audio", "opus/16000/1")
		}
		r, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		b, _ := io.ReadAll(r.Body)
		r.Body.Close()
		return r, b
	}
	if r, b := post(false); r.StatusCode != http.StatusBadRequest {
		t.Errorf("audio without X-Audio: %s %s", r.Status, b)
	}
	if r, b := post(true); r.StatusCode != http.StatusOK || !strings.Contains(string(b), fmt.Sprintf(`"audio":%d`, packets)) {
		t.Fatalf("%s %s (%d packets after the first picture sent)", r.Status, b, packets)
	}
	r, err := http.Get(s.URL + "/v1/media/" + id)
	if err != nil {
		t.Fatal(err)
	}
	mp4 := ok(t)(io.ReadAll(r.Body))
	r.Body.Close()
	if out := os.Getenv("LEAKCAM_MP4_OUT"); out != "" {
		os.WriteFile(out, mp4, 0o644)
	}
	var p pmp4.Presentation
	if err := p.Unmarshal(bytes.NewReader(mp4)); err != nil {
		t.Fatal(err)
	}
	if len(p.Tracks) != 2 {
		t.Fatalf("%d tracks", len(p.Tracks))
	}
	a := p.Tracks[1]
	if _, isOpus := a.Codec.(*mp4codecs.Opus); !isOpus || a.TimeScale != AudioClock || len(a.Samples) != packets {
		t.Errorf("audio track %T, timescale %d, %d samples (want %d)", a.Codec, a.TimeScale, len(a.Samples), packets)
	}
	if a.TimeOffset != 20000*AudioClock/1e6 {
		t.Errorf("audio starts at %d (48 kHz), want 20 ms after the first picture", a.TimeOffset)
	}
}
