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

### Automated run 2026-11-14 (run.py, 59 s): pass

9/9 checks pass.


### Automated run 2026-11-14 (run.py, 45 s): pass

9/9 checks pass.


### Automated run 2026-11-12 (run.py, 52 s): pass

9/9 checks pass.


### Automated run 2026-11-12 (run.py, 48 s): pass

9/9 checks pass.


### Automated run 2026-11-12 (run.py, 57 s): pass

9/9 checks pass.


### Automated run 2026-11-11 (run.py, 48 s): pass

9/9 checks pass.


### Automated run 2026-11-10 (run.py, 45 s): pass

9/9 checks pass.


### Automated run 2026-11-10 (run.py, 44 s): **FAIL**

6/9 checks pass. Failed: no clock: the check-in taken (dates not checked) and its X-Time sets the clock (tls: TLSv1.3 TLS1-3-AES-128-GCM-SHA256 in 344 ms, no clock: dates not checked, session heap 15940 B; clock 0 vs 1794354398); a clock in 2049 (past the 20 years): the server's certificate refused, nothing sent (8.1.1
{FVIF-0} ip=I4 gw=I4[0] wl1: MAC=7c:3e:82:62:b1:49 ip=192.168.1.222/24 UP,CONNECTED
tls: TLSv1.3 TLS1-3-AES-128-GCM-SHA256 in 346 ms, session heap 15932 B); the refused session did not move the clock (1794354400).


### Automated run 2026-11-10 (run.py, 50 s): **FAIL**

6/9 checks pass. Failed: a clock in 2049 (past the 20 years): the server's certificate refused, nothing sent (8.1.1
{FVIF-0} ip=I4 gw=I4[0] wl1: MAC=7c:3e:82:62:b1:49 ip=192.168.1.222/24 UP,CONNECTED
tls: TLSv1.3 TLS1-3-AES-128-GCM-SHA256 in 897 ms, session heap 15924 B); the refused session did not move the clock (1794354316); no clock again: taken, the clock set from the verified answer (clock None).


### Automated run 2026-11-10 (run.py, 73 s): pass

9/9 checks pass.


### Automated run 2026-11-10 (run.py, 58 s): **FAIL**

7/9 checks pass. Failed: no clock again: taken, the clock set from the verified answer (clock 0); 10 sessions: every one up, the heap kept (after the 1st vs after the last) (9/10 up, free (171040, 141408) -> (143924, 141408)).


### Automated run 2026-11-10 (run.py, 31 s): pass

9/9 checks pass.


### Automated run 2026-11-10 (run.py, 37 s): pass

9/9 checks pass.


### Automated run 2026-11-10 (run.py, 41 s): **FAIL**

7/9 checks pass. Failed: no clock: the check-in taken (dates not checked) and its X-Time sets the clock (tls: TLSv1.3 TLS1-3-AES-128-GCM-SHA256 in 976 ms, session heap 15928 B; clock 1794299612 vs 1794299613); 10 sessions: every one up, the heap kept (10/10 up, free (171060, 141440) -> (143828, 141380)).


### Automated run 2026-11-10 (run.py, 52 s): **FAIL**

8/9 checks pass. Failed: 10 sessions: every one up, the heap kept (9/10 up, free (133248, 102132) -> (133248, 102124)).


No run yet.

## Obstacles

- **2026-11-10 23:45 UTC, the date steps raced the check-in's answer (test side).** session() stopped at the `tls:` line,
  printed at the handshake; the answer with its X-Time comes after it. The clock was read too early (0 after the no-clock
  session) and the late X-Time overwrote the 2049 clock the test set next, so the "2049" session ran with the real clock
  and passed. Reproduced alone. Fix: session() waits for the check-in's end (`[WCHK] check of ...: state 0x..`). Then 9/9.
