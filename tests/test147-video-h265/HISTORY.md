# test147 history

## 2026-11-12: built and brought up

- First run: 2 frames, then `mvx_mmu_alloc_page: fail`: the VPU takes its working pages from RT-Smart's page
  allocator. Measured with `list_page` during a one-camera video: ~14 MB per native H.265 channel; 27.8 MB were free.
  Fixed: RT-Smart 48 -> 52 MB, MMZ 80 -> 76 MB; the leak check's 5 MB history allocated per check; the JPEG channel
  closed for a video; the stream pools smaller (H.265 w*h/8 x 3, JPEG w*h/4 x 2).
- The server read the chunked end marker as frame data ("frame 15: length 806160909" = "0\r\n\r"): a send had
  failed in the middle of a chunk and the K230 still sent the end. Fixed: after a failed send the socket is closed.
- Camera 1's upload cut after ~1 s every time: lwIP `send()` said ENOMEM at once (not a dead link). Fixed: retried
  until the 10 s I/O limit. With 2 x 4 Mbit/s the bridge then still fell behind (a 2 MB backlog, ENOMEM for 10 s):
  2 x 2 Mbit/s goes through.
- The live view never started ("SPS not received yet"): the encoder's first fetch brings VPS/SPS/PPS alone. Fixed on
  the server (the parameter sets in front of every IDR to the HLS muxer) and the K230 (the picture fetched after a
  header-only fetch: 225 frames, not 224).
- The clips played twice as fast: 7.5 fps frames stamped 15 fps. Fixed: each frame carries its own time (pts) to the
  server; a clip ends after 15 s of the frames' time.
- The board switched to the setup firmware once (3 check-ins failed after two half-finished measuring runs left
  videos queued): back with `slot switch`; with the fixes the check-ins after a video all pass.

## 2026-11-12 20:00-20:40 UTC+2: why 7.5 fps (sim-file test tokens `cams=1`, `vmode=`, `inbuf=`, job.c)

- The sensor runs 15 fps: OV5647 registers read over I2C on both buses: PLL 0x3034-0x3037 = 1a 21 69 03, HTS 2844,
  VTS 2051 (87.5 MHz pixel clock, 14.4-15 fps), output 2592x1944.
- Camera 0 alone, offline, 2 raw buffers: 7.5 fps (first run, before the sequence log): not the two cameras sharing.
- The frames' sequence numbers (time_ref, logged since): two cameras offline: 114 frames, sequence 76..189 (+1 each):
  VICAP itself puts out 7.5 fps, our loop drops none.
- Camera 0 alone, online mode: VICAP 15 fps (sequence 152..378, +2 per frame we got), but the dump loop gets 7.5.
- Camera 0 alone, offline, 4 raw buffers: **15.0 fps** (224 frames, sequence 153..376); 3 raw buffers: 15.0 fps too.
- Both cameras, 3 raw buffers: camera 1's open fails: media memory 29.5 + 14.7 MB per camera (/proc/media-mem), 31 MB
  left after camera 0.
- Camera 0 online + camera 1 offline with 3 (fits the memory): both open and the leak check works, but the light
  probe's 320x240 dumps fail on both, the encoder runs out of pages (mvx_mmu_alloc_page) and camera 0 gives no frames.
- Obstacles: the console's `echo` takes the file as its 2nd argument (`echo cams=1 /sdcard/leakcam/sim`, no `>>`); a
  NAND flash erases /sdcard (the tokens and histories go); `rm` of the sim file then `echo` cannot create it again
  (the job creates it at its next check); the VM's USB controller dropped the board once after a reboot
  (hub_ext_port_status -32): xhci_reset().

## 2026-11-12 20:45-21:15 UTC+2: raw frames 10-bit packed (user: "please try yourself 10 bit")

- The register map: VeriSilicon ISP (nxp-imx/isp-vvcam vvcam/isp/mrv_all_regs.h, the same IP): the raw writer is the
  MIV2 MCM block, 0x90001600..: miv2_mcm_fmt 0x1604 = rd / wr0 / wr1 raw bit (4 bits each, bits 0 / 4 / 8) and their
  alignment (bits 12-17, 0 = unaligned = packed); line lengths 0x1620 / 0x164c (write), 0x1674 (read DMA).
- Read on the board (kernel msh `ispreg`, sys_stat.c, experiment): fmt 0x444 (raw bit 4 = 16-bit words), line
  length 0x1440 = 2592 x 2, size 0x99c600 = 5184 x 1944: the 16-bit layout, as the SDK's buffer size says.
- The closed libraries (disassembled): user-space libisp VideoInSetMcmWrCfg -> isp_mcm_cfg_wr sets the old MCM
  control (0x1200, format code 4 for both cameras); the MIV2 format is set by the kernel side each frame.
- Written from the console with the stream running (camera 0, 2 buffers): fmt 0x111 + packed line lengths read back
  as 0x154 at once (the driver writes it again per frame), the stream stopped (dumps failed), the cameras' close
  failed (vb exit): packed raw needs the closed driver itself changed (a binary patch of the kernel library), not a
  register write. The program cannot map the ISP's registers (kd_mpi_sys_mmap refuses); an ioctl through
  /dev/sysstat returned -1 (not followed: the console way was quicker).

## 2026-11-12 21:15-23:00 UTC+2: the packed-raw patch, both cameras 15 fps

- Trace build (rawpack=2): the MCM register writes, their values and callers (firmware/VIDEO.txt section 6).
- Patched, 16-bit buffers: a sheared picture (fmt read back 0x51: the shadow's OR, then 3240 B rows vs the
  writer's 16 B padding), then black (the direct store kept 16-bit), then right with the shadow patched, VIHalWriteReg
  replaced and 3248 B rows: same picture as unpatched, mean 88 = 88.
- 3 packed buffers, camera 0: init failed ("buffer size(10078208)!=(6315008) check failed"): VsiCamDeviceGetBufferSize
  wrapped; an own pool of small blocks failed in the kernel (it takes buffer_size blocks). Then 15.0 fps, 225 frames.
- Both cameras, 3 packed buffers: 14.9 / 15.0 fps, both MP4s 225 frames, 67.5 MB media memory. One run lost one upload
  ("http: send failed: Out of memory": the trace's console flood then; trace off for the patch), the next both
  uploaded, the run on the product default (tokens removed) lost both after 15 frames: lwIP memory at ~3.4 Mbit/s.
- Obstacles: VIHalWriteReg's replacement returned void (callers check it): fixed to 0; the common-register mask is
  0x1111 (0x160c too), read wrong as 0x111 first; objcopy --globalize and --weaken in one call left the symbol
  strong (two calls); "dump dev(N)chn(1) failed" is cap_ambient's drain (timeout 0), not a fault: a bisect build
  without the patch showed the same; the console's USB dropped once after a reboot (xhci_reset).
