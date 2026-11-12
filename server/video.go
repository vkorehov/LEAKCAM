package main

// The video stream from the board (user 2026-11-12: "h265 with native resolution 15 frames", "add streaming server
// to be a golang, same server", "just add lib"; DESIGN.md 5.3.14): the K230 encodes each camera's native 2592x1936
// frames to H.265 and POSTs them as they come (one chunked upload per camera, POST /v1/media/<id>-v<n>, Content-Type
// VideoType): frames of [u32 BE length][u64 BE the frame's time, us, the camera's clock][one access unit, Annex-B];
// the frames' own times give the rate (the cameras' real one, any dropped frame). The server
//   - serves them live as HLS while the upload runs (GET /v1/live/<id>/index.m3u8, gohlslib), and
//   - writes the clip as an MP4 when the upload ends (mediacommon's pmp4), its creation time the board's X-Taken,
//     so the app plays an ordinary file (GET /v1/media/<id>).

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"sync"
	"time"

	"github.com/bluenviron/gohlslib/v2"
	hlscodecs "github.com/bluenviron/gohlslib/v2/pkg/codecs"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h264"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h265"
	mp4codecs "github.com/bluenviron/mediacommon/v2/pkg/formats/mp4/codecs"
	"github.com/bluenviron/mediacommon/v2/pkg/formats/pmp4"
)

const (
	VideoType     = "video/x-leakcam-h265" // the board's framing: [u32 BE length][u64 BE pts us][access unit, Annex-B] ...
	VideoClock    = 90000                  // the timescale (the usual one for video)
	VideoFrameMax = 4 << 20                // one access unit at most (a native IDR is ~0.1-0.3 MB at 2 Mbit/s)
)

var (
	liveMu sync.Mutex
	live   = map[string]*gohlslib.Muxer{} // the uploads running now, by media ID
)

// putVideo: the board's H.265 upload (putMedia hands it over for a v<n> ID with Content-Type VideoType); the reply
// after the last frame: {"stored":true,"frames":N}
func putVideo(w http.ResponseWriter, r *http.Request, id, path string) {
	taken := time.Now()
	if ms, err := strconv.ParseInt(r.Header.Get("X-Taken"), 10, 64); err == nil && ms > 0 {
		taken = time.UnixMilli(ms)
	}
	v := &videoIn{id: id, taken: taken}
	defer v.endLive()
	err := v.read(http.MaxBytesReader(w, r.Body, MediaMax))
	if err == nil && len(v.samples) == 0 {
		err = errors.New("no frame")
	}
	if err == nil {
		err = v.writeMP4(path)
	}
	if err != nil {
		log.Printf("media %s: video: %v (%d frames)", id, err, len(v.samples))
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	os.Chtimes(path, taken, taken)
	span := float64(v.pts[len(v.pts)-1]-v.pts[0]) / 1e6
	log.Printf("media %s stored: %d frames over %.1f s (%.1f fps)", id, len(v.samples), span,
		float64(len(v.samples)-1)/max(span, 1e-3))
	reply(w, map[string]any{"stored": true, "frames": len(v.samples)})
}

type videoIn struct {
	id            string
	taken         time.Time
	pts           []int64 // each sample's time, us (the camera's clock)
	vps, sps, pps []byte
	samples       [][]byte // AVCC access units without the parameter sets (they go into hvcC)
	sync          []bool
	hls           *gohlslib.Muxer
	track         *gohlslib.Track
}

// read: frame after frame until the body ends; each frame into the clip and to the live viewers
func (v *videoIn) read(body io.Reader) error {
	var hdr [12]byte
	for {
		if _, err := io.ReadFull(body, hdr[:]); err != nil {
			if errors.Is(err, io.EOF) {
				return nil // the upload ended between two frames: the clip is whole
			}
			return fmt.Errorf("frame %d length: %w", len(v.samples), err)
		}
		n := binary.BigEndian.Uint32(hdr[:4])
		pts := int64(binary.BigEndian.Uint64(hdr[4:]))
		if n == 0 || n > VideoFrameMax {
			return fmt.Errorf("frame %d: length %d", len(v.samples), n)
		}
		buf := make([]byte, n)
		if _, err := io.ReadFull(body, buf); err != nil {
			return fmt.Errorf("frame %d: %w", len(v.samples), err)
		}
		if err := v.frame(buf, pts); err != nil {
			return fmt.Errorf("frame %d: %w", len(v.samples), err)
		}
	}
}

func (v *videoIn) frame(annexb []byte, pts int64) error {
	var au h264.AnnexB
	if err := au.Unmarshal(annexb); err != nil {
		return err
	}
	var vcl [][]byte
	for _, nalu := range au {
		if len(nalu) < 2 {
			continue
		}
		switch h265.NALUType((nalu[0] >> 1) & 0x3f) {
		case h265.NALUType_VPS_NUT:
			v.vps = nalu
		case h265.NALUType_SPS_NUT:
			v.sps = nalu
		case h265.NALUType_PPS_NUT:
			v.pps = nalu
		case h265.NALUType_AUD_NUT:
		default:
			vcl = append(vcl, nalu)
		}
	}
	if !slices.ContainsFunc(vcl, func(n []byte) bool { return (n[0]>>1)&0x3f < 32 }) {
		return nil // no slice: parameter sets (and SEI) alone, the encoder's header packet
	}
	if len(v.samples) == 0 && !h265.IsRandomAccess(au) {
		return errors.New("the first frame is not an IDR")
	}
	sample, err := h264.AVCC(vcl).Marshal()
	if err != nil {
		return err
	}
	if len(v.pts) > 0 && pts <= v.pts[len(v.pts)-1] {
		return fmt.Errorf("time %d us not after %d", pts, v.pts[len(v.pts)-1])
	}
	v.samples = append(v.samples, sample)
	v.pts = append(v.pts, pts)
	v.sync = append(v.sync, h265.IsRandomAccess(au))
	if h265.IsRandomAccess(au) { // the parameter sets in front of every IDR: the K230's encoder sends them once,
		// as a packet of their own before the first frame, and the live muxer reads them from the frames
		au = append([][]byte{v.vps, v.sps, v.pps}, vcl...)
	}
	v.toLive(au)
	return nil
}

// toLive: the frame to the HLS muxer (started at the first IDR, when the parameter sets are known); a failure there
// only ends the live view, never the clip
func (v *videoIn) toLive(au [][]byte) {
	if v.hls == nil {
		if v.vps == nil || v.sps == nil || v.pps == nil {
			return
		}
		v.track = &gohlslib.Track{Codec: &hlscodecs.H265{VPS: v.vps, SPS: v.sps, PPS: v.pps}, ClockRate: VideoClock}
		m := &gohlslib.Muxer{
			Tracks:             []*gohlslib.Track{v.track},
			Variant:            gohlslib.MuxerVariantFMP4,
			SegmentCount:       7,
			SegmentMinDuration: time.Second,
		}
		if err := m.Start(); err != nil {
			log.Printf("media %s: live: %v", v.id, err)
			return
		}
		v.hls = m
		liveMu.Lock()
		live[v.id] = m
		liveMu.Unlock()
	}
	us := v.pts[len(v.pts)-1] - v.pts[0]
	ntp := v.taken.Add(time.Duration(us) * time.Microsecond)
	if err := v.hls.WriteH265(v.track, ntp, us*VideoClock/1e6, au); err != nil {
		log.Printf("media %s: live: %v", v.id, err)
	}
}

func (v *videoIn) endLive() {
	if v.hls == nil {
		return
	}
	liveMu.Lock()
	delete(live, v.id)
	liveMu.Unlock()
	v.hls.Close()
}

// writeMP4: the clip, written whole then renamed (a reader never sees half a file), its creation time the board's
func (v *videoIn) writeMP4(path string) error {
	if v.vps == nil || v.sps == nil || v.pps == nil {
		return errors.New("no VPS/SPS/PPS")
	}
	track := &pmp4.Track{
		ID:        1,
		TimeScale: VideoClock,
		Codec:     &mp4codecs.H265{VPS: v.vps, SPS: v.sps, PPS: v.pps},
	}
	last := uint32(VideoClock / 15) // the last frame's duration: the average one (one frame: 1/15 s)
	if n := len(v.pts); n > 1 {
		last = uint32((v.pts[n-1] - v.pts[0]) * VideoClock / 1e6 / int64(n-1))
	}
	for i, s := range v.samples {
		s := s
		d := last
		if i+1 < len(v.pts) { // the time to the next frame, on the 90 kHz clock
			d = uint32((v.pts[i+1]-v.pts[0])*VideoClock/1e6 - (v.pts[i]-v.pts[0])*VideoClock/1e6)
		}
		track.Samples = append(track.Samples, &pmp4.Sample{
			Duration:        d,
			IsNonSyncSample: !v.sync[i],
			PayloadSize:     uint32(len(s)),
			GetPayload:      func() ([]byte, error) { return s, nil },
		})
	}
	var b bytes.Buffer
	if err := (&pmp4.Presentation{Tracks: []*pmp4.Track{track}}).Marshal(&b); err != nil {
		return err
	}
	out := b.Bytes()
	setCreation(out, v.taken)
	f, err := os.CreateTemp(filepath.Dir(path), ".upload-*")
	if err != nil {
		return err
	}
	_, err = f.Write(out)
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(f.Name(), path)
	}
	if err != nil {
		os.Remove(f.Name())
	}
	return err
}

// setCreation: the moment into the mvhd, tkhd and mdhd (version 0: creation and modification after the version and
// flags, seconds since 1904): galleries show it as the video's date (pmp4 leaves them 0)
func setCreation(mp4 []byte, t time.Time) {
	s := uint32(t.Unix() + 2082844800)
	for _, box := range []string{"mvhd", "tkhd", "mdhd"} {
		i := bytes.Index(mp4, []byte(box))
		if i < 4 || i+16 > len(mp4) || mp4[i+4] != 0 {
			continue
		}
		binary.BigEndian.PutUint32(mp4[i+8:], s)
		binary.BigEndian.PutUint32(mp4[i+12:], s)
	}
}

// liveVideo: GET /v1/live/{id}/{file}: the HLS of an upload running now (index.m3u8 first); 404 when none runs
func liveVideo(w http.ResponseWriter, r *http.Request) {
	liveMu.Lock()
	m := live[r.PathValue("id")]
	liveMu.Unlock()
	if m == nil {
		http.NotFound(w, r)
		return
	}
	m.Handle(w, r)
}
