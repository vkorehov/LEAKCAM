# test130-ota-upgrade: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### By hand 2026-11-06 (bench, server on the Pi): BL616 steps 1-4, 6, 13 pass

| Step | Offer | Result |
|---|---|---|
| 1-2 | 0.6+g80b8d44 -> bc32e53 | copy 600584 B 6988 ms, sum 86 ms, running at the check-in 27 s after the offer |
| 6 | 1600000 B image (padded) | `does not fit: image 1600000 B + copy 600592 B (running image 1098256 B) in slot 0's 2097152 B`, once |
| 4 | one .xz byte flipped, the offer's sum the good one | read-back sum 60f21bf5... = the corrupted file's: the running firmware stays |
| 3 | the server stopped 3 s into the download | `download failed ... after 486692 of 600592 B`: stays; next check-in: copy 7905 ms, upgraded in 27 s, `sw 2` |
| 13 | 3f9077b | `sw 2`, boots +2 (after 84a2007's ota.c fix; `sw 1` before) |
| 5 | console `reboot` 2.6 s into the unpack | not run by the shell while the unpack held the chip: unpack + write + check 11.3 s, upgraded |

## Obstacles

- 2026-11-06, the fourth offer upgraded (27 s offer to running) but the check-in said `sw 1` for two switches: the
  setup firmware's switch back after the unpack (ota.c) called slot_switch() without metrics_switch(). Fixed in
  84a2007, `sw 2` since.
- 2026-11-06, the console logger (a scratch script) kept reading the old fd after the BL616 re-enumerated (the fd
  goes silent, no error): it missed the setup firmware's lines; reopened on a new USB device number since.

- 2026-11-06, the third offer (0.6+g4a2f6a5 -> 0.6+ge3b298c): the sum wrong again (c6ba3c44...), the copy read back
  through the ROM (BLFlashCommand --read --flash) byte-exact (d4c39f21..., the .xz's): the sum, not the download.
  flash_sha.c drove the SEC engine directly while Wi-Fi ran; the supplicant shares the engine under the SDK's
  bflb_sec_mutex (mbedtls port hw_acc/sha256_alt.c), which a direct use never takes. Now through mbedtls (80b8d44).

- 2026-11-06, the second offer (0.6+g635915b -> 0.6+g9185ae5): no crash, the copy downloaded in ~7 s, but its
  SHA-256 by read-back was wrong and different each time (61d430dc..., then 4cac9a6c...; the .xz's is 912a273d...):
  flash_sha.c kept its 512 B buffer and the SHA context in cached RAM, the SEC engine reads memory past the cache; the
  SDK's sec_eng_sha example keeps both in ATTR_NOCACHE_NOINIT_RAM_SECTION, the input 32-aligned. Fixed in 4a2f6a5. The
  check worked as meant: the running firmware stayed each time.

- 2026-11-06, the first offer (full 0.6+ga4a09a9 -> 0.6+gc2101c1): the full firmware crashed in the download's
  check, ~14 s after every boot, at every check-in after it (crash record `exception in task 'pres'`, mcause
  0x38000003, crash_decode.py: bflb_lhal_assert_func <- bflb_device_get_by_name <- flash_sha256 <- ota_fetch):
  flash_sha.c asked for the device "sha", the SDK names the SHA engine "sec_sha" (BFLB_NAME_SEC_SHA). Never ran before
  this test. The image taken off the server to stop the loop; fixed in 635915b (both firmwares: the setup firmware's
  unpack checks with the same function).
