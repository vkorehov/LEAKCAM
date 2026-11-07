# test109: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

The first plan named the first product's requests (`/v1/check` from leakcam_wake, the change net by CRC from
`/v1/nn/<CRC>`) and waited for NTP when the board had no clock. Now: the check-in `POST /v1/poll`, the per-device
model `GET /v1/devices/{id}/model` (SHA-256, test130 steps 9-11), no NTP (every server answer carries `X-Time`), and
after a battery loss the first TLS connection trusts the pinned server key. The old lines:

    Today every request is plain HTTP / RTMP without authentication (SECURITY.txt 4). Needed before real use: TLS for
    `/v1/check`, `/v1/nn`, `/v1/poll` and the clips (RTMPS to MediaMTX); ...

    K230: TLS client for the three requests and RTMPS (library to choose: mbedTLS in the SDK, or what RT-Smart offers),
    the device key storage, time from NTP for certificate dates; ...

    5. Signed change net: a net with the right CRC but a bad signature is refused (test107's update path).
    7. Replay: a recorded `/v1/check` sent again is rejected (nonce or time window).
    8. No clock: the first wake after a total power loss (no RTC time) still validates the certificate or waits for NTP -
       defined behaviour, no plain fallback.

## Runs

No run yet.

## Obstacles

No obstacle recorded yet.
