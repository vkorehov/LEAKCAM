@echo off
rem The LEAKCAM server on the Windows PC (wired Ethernet, user 2026-11-12: the Pi on 2.4 GHz Wi-Fi limited the video
rem uploads to ~10 Mbit/s in all). Started at boot by the scheduled task "LEAKCAM server" (install.ps1), as SYSTEM.
rem env: LEAKCAM_CHECKIN_S and LEAKCAM_PIN_KEY, one KEY=value a line (as the Pi's ~/leakcam-server/env; never in git).
set D=C:\hw\leakcam-server
cd /d %D%
for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%D%\env") do set "%%a=%%b"
set LEAKCAM_MEDIA=%D%\media
set LEAKCAM_IMAGES=%D%\images
set LEAKCAM_MODELS=%D%\models
set LEAKCAM_TLS=%D%\tls
set LEAKCAM_FCM=%D%\fcm.json
set LEAKCAM_TOKENS=%D%\tokens.json
set LEAKCAM_PHONES=%D%\phones.json
set LEAKCAM_THRESHOLDS=%D%\thresholds.json
set LEAKCAM_METRICS=%D%\metrics.jsonl
set LEAKCAM_CRASHES=%D%\crashes.jsonl
"%D%\leakcam-server.exe" >> "%D%\server.log" 2>&1
