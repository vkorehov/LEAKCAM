# test130-ota-upgrade: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

## Obstacles

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
