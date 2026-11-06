# test130: OTA upgrade of the BL616 full firmware and the K230 image

Status: **BL616 part built, not run** (2026-11-06); K230 image and per-device model update planned (DESIGN.md 5.3.7). Both upgradeable over the air; the BL616 setup
firmware is not, it changes only by wire (`make flash`). Started by the server, automatically: the check-in answer
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
   (checksum mismatch, power cut during the write) -> the SPL boots the old slot.
8. Both versions reported to the server after the upgrade.
9. **The model, per device:** a model for this device ID on the server (and a different default): the check-in
   answer names this device's model (version, SHA-256, size); `GET /v1/devices/{id}/model` gives that file, another
   device ID gets the default; the board writes `<name>.new`, checks it, renames it in, keeps `.old`.
10. **The model and the image apart:** a model update leaves both slots and `ota_meta` untouched; an image update
    leaves the model file untouched.
11. **A bad model:** one that fails to load or its known-output check: the board falls back to `.old` and reports it;
    the server's next offer is not retried in a loop (one try per offered version).
12. **The program in the slot (ROMFS):** after an image update the program that runs is the new slot's (its build tag
    in the log), with no file on `/sdcard/app`.

## Needs (build features and dependencies)

Server: the image store and the offer (built). BL616: ota_fetch.c (full), ota.c with the xz decoder (setup),
flash_sha.c, the streaming download in wifi_check.c (built). K230: the OTA writer (inactive slot + `ota_meta`), not
built.
