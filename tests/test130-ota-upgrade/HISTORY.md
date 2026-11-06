# test130-ota-upgrade: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

## Obstacles

- 2026-11-06, the first offer (full 0.6+ga4a09a9 -> 0.6+gc2101c1): the full firmware crashed in the download's
  check, ~14 s after every boot, at every check-in after it (crash record `exception in task 'pres'`, mcause
  0x38000003, crash_decode.py: bflb_lhal_assert_func <- bflb_device_get_by_name <- flash_sha256 <- ota_fetch):
  flash_sha.c asked for the device "sha", the SDK names the SHA engine "sec_sha" (BFLB_NAME_SEC_SHA). Never ran before
  this test. The image taken off the server to stop the loop; fixed in 635915b (both firmwares: the setup firmware's
  unpack checks with the same function).
