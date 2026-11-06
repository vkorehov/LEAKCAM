# test130: OTA upgrade of the K230 image and the BL616 full firmware

Status: **planned** (2026-11-05, user): both upgradeable over the air; the BL616 setup firmware is not, it changes only
by wire (`make flash`). Started by the server, automatically: the check-in answer names the newest version and a
board on an older one installs it with no prompt (user, 2026-11-05); every image checksum-verified before the switch
(DESIGN.md 5.3.7).

## Steps (when built)

1. The server offers a new version in the check-in answer; the board starts by itself, no prompt anywhere.
2. K230: the new image downloaded into the inactive slot (rtt_a/rtt_b), `ota_meta` switched, the next boot runs it.
3. K230: a broken image (checksum mismatch, power cut during the write) -> the SPL boots the old slot; nothing lost.
4. BL616 full firmware: the new image into its inactive area, the partition table switched, the board checks in on it.
5. BL616: a broken download or a power cut mid-write -> the old full firmware runs; the setup firmware untouched.
6. Both versions reported to the server after the upgrade.

## Needs (build features and dependencies)

The K230 OTA writer (inactive slot + `ota_meta`, not built), the BL616 full-firmware writer and its slot switch, the
server's image store and version check.
