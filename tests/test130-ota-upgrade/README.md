# test130: OTA upgrade of the BL616 full firmware and the K230 image

Status: **BL616 part: steps 1-4, 6, 8 (BL616), 13 (BL616) pass** (2026-11-06, by hand on the bench with the server on
the Pi, BL616 pair 0.6+g84a2007): the offer at a check-in, the copy (600 KB) in 7-8 s, its SHA-256 read back in 86 ms,
the setup firmware's unpack + write + check of slot 0 in 11.3 s, the new version's check-in 27-29 s after the offer
(`sw 2`, boots +2, no crash). Refused as meant: an image that does not fit (one line per version), a corrupted copy
(the read-back sum is the corrupted file's), a download cut by the server stopped (486692 of 600592 B: the old
firmware runs on, the next check-in downloads again and upgrades). Four bugs fixed on the way (HISTORY.md). Open:
step 5 (a reset in the 11 s unpack: an EN tap by hand; `reboot` on the console is not run while the unpack holds the
chip), the K230 steps 7, 9-12 (the K230 OTA writer is not built).

Both upgradeable over the air; the BL616 setup firmware is not, it changes only by wire (`make flash`). Started by the server, automatically: the check-in answer
names the newest version and a board on an older one installs it with no prompt (user, 2026-11-05); every image
checksum-verified before the switch (DESIGN.md 5.3.7).

## How the BL616 part works (DESIGN.md 5.3.7, user 2026-11-06)

The old full image is never erased before the whole new one is on the board: a Wi-Fi loss mid-download must not leave
the board without its full firmware (no leak detection).

1. The server (LEAKCAM_IMAGES: `bl616_full.bin` + the SDK build's `bl616_full.xz`) answers a full firmware's check-in
   of another version with `full`, `full_sha`, `full_size`, `xz_sha`, `xz_size`.
2. The full firmware, still running, checks that image + copy fit slot 0 beside its own image, downloads the `.xz` from
   `GET /v1/images/bl616_full` into the free end of slot 0 and checks its SHA-256 by read-back. A failure changes
   nothing: the next check-in tries again.
3. It switches to the setup firmware with reason `ota`. The setup firmware checks the copy again, sets `full_broken`,
   unpacks it over the old image (xz, no network), checks the result's SHA-256, clears the keys and switches back.
4. A power cut during the unpack: the setup firmware starts the unpack again from the intact copy; `full_broken` keeps
   every other way to slot 0 closed meanwhile.

## Steps

1. Server: a newer `bl616_full.bin`/`.xz` in LEAKCAM_IMAGES; the board's next check-in gets the offer (server log
   `full <old> -> <new> offered`), no prompt anywhere.
2. BL616: the copy downloaded and checked while the full firmware runs (`ota: copy <B> in <ms> ms, SHA-256 ok`), the
   switch to setup, the unpack (`ota: slot 0 written and checked`), the board back on the full firmware with the new
   version (`slot`), checked in with it (the server's next answer has no offer).
3. BL616: Wi-Fi lost mid-download (the AP off, or the server stopped): the old full firmware keeps running, no switch;
   the next check-in downloads again.
4. BL616: a corrupted image on the server (`.xz` changed, the SHA in the answer not): the read-back check fails, the
   old firmware runs on.
5. BL616: power cut during the unpack (BL616 reset by hand mid-way): the setup firmware unpacks again from the copy and
   ends on the new full firmware; the setup firmware untouched.
6. BL616: an image that does not fit (image + copy over slot 0's 2 MB): refused, one log line, nothing written.
7. K230: the new image into the inactive slot (rtt_a/rtt_b), `ota_meta` switched, the next boot runs it; a broken image
   (checksum mismatch, power cut during the write) -> the SPL boots the old slot. Bench, no network: a kdimg whose
   /sdcard holds a second slot image; msh `ota_records` (A none, B none: A boots), `ota_file /sdcard/<image>` (slot B
   written, read back, record 1), reboot: the SPL log names slot B and `ota_records` says B boots; a slot B image
   corrupted on purpose (one byte flipped in its file before ota_file is refused; one flipped in NAND after the
   record) -> the SPL's SHA-256 fails and slot A boots.
8. Both versions reported to the server after the upgrade.
9. **The model, per device:** a model for this device ID on the server (and a different default): the check-in
   answer names this device's model (version, SHA-256, size); `GET /v1/devices/{id}/model` gives that file, another
   device ID gets the default; the board writes `<name>.new`, checks it, renames it in, keeps `.old`.
10. **The model and the image apart:** a model update leaves both slots and `ota_meta` untouched; an image update
    leaves the model file untouched.
11. **A bad model:** one that fails to load or its known-output check: the board falls back to `.old` and reports it;
    the server's next offer is not retried in a loop (one try per offered version).
12. **The program in the slot (ROMFS):** after an image update the program that runs is the new slot's (the K230
    log's `version <ver>` line, and `kver=<ver>` in the next check-in, DESIGN.md 5.3.10), with no file on
    `/sdcard/app`.
13. **Versions in the metrics:** after the BL616 upgrade the check-in's `full=` is the new version and `boots` up by
    the switches (to setup for the unpack and back: `sw=2`); after a K230 update a session's STATUS gives the new
    `kver`; a rolled-back K230 image (step 7's corrupted slot) shows the old `kver`.

## Needs (build features and dependencies)

Server: the image store and the offer (built). BL616: ota_fetch.c (full), ota.c with the xz decoder (setup),
flash_sha.c, the streaming download in wifi_check.c (built). K230: the OTA writer (inactive slot + `ota_meta`), not
built.

## Metrics (2026-11-06 BL616 runs; skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | the new full firmware's first check-in 6-8 s after its start | test022 | |
| This part's boot share | n/a: the OTA runs after the check-in, not at boot | - | |
| Test run time | offer -> running 27-29 s per upgrade | - | |
| Download | 600 KB in 6988-7905 ms (~80 KB/s) | - | over the board's Wi-Fi, server on the Pi |
| Copy check | SHA-256 read back 86 ms (600 KB) | - | mbedtls on the SEC engine |
| Unpack | setup firmware: xz unpack + write + check of slot 0 11.3 s | - | |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Current idle / during | n/a: on USB power, not metered | floor (test031) | |
| Power, energy per operation | n/a: not metered | - | |
| Per day | n/a: once per release | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | BL616 die 37-39 C (check-ins around the upgrades) | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 193456 before, 194000 after the upgrade, minfree 187976-188084: no leak | no leak | |
| Image (sizes) | full 1098256 B, its .xz 600584-601028 B | minimal image (rule) | |
| Speed / rate against the top | 27 s offer to running, of it ~7.5 s download, 11.3 s unpack | SPEEDS.txt | |
| Compile flags | BL616 product -Os -g (SDK Kconfig default), no LTO | smallest code (rule) | |
| Features over the minimal image | ota_fetch.c (full), ota.c with xz (setup), flash_sha.c | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
