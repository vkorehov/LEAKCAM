# Restart the LEAKCAM server on the Windows PC, optionally with a new build (run as an administrator; the server and
# its scheduled task "LEAKCAM server" run as SYSTEM, so an ordinary user cannot stop it: 2026-11-13 a plain taskkill
# was refused silently and the old server kept running from the renamed exe).
#   restart.ps1 [-New <path of a new leakcam-server.exe>]
# Steps: the task stopped (no restart of start.bat in between), the server process stopped by its PID, the new exe
# swapped in (the previous one kept as leakcam-server.exe.old), the task started, port 8090 answering within 20 s.
# Everything goes to restart.log next to the server too (an elevated window's output is not seen by its caller).
param([string]$New = "")
$ErrorActionPreference = "Stop"
$d = 'C:\hw\leakcam-server'
$exe = Join-Path $d 'leakcam-server.exe'
$log = Join-Path $d 'restart.log'
function Say($m) { $l = "$(Get-Date -Format s) $m"; Write-Output $l; Add-Content -Path $log -Value $l }
# any error ends the script: logged first (2026-11-13 21:13 it died silently at Stop-Process, the process already ended
# by stopping the task, and left the server down until the next run)
trap { Say "FAIL: $_ (line $($_.InvocationInfo.ScriptLineNumber))"; exit 1 }

Say "restart: task $((Get-ScheduledTask -TaskName 'LEAKCAM server').State)"
Stop-ScheduledTask -TaskName 'LEAKCAM server'
foreach ($p in @(Get-Process leakcam-server -ErrorAction SilentlyContinue)) {
    Say "stop pid $($p.Id)"
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue   # often gone already: stopping the task ends it
    for ($i = 0; $i -lt 50 -and (Get-Process -Id $p.Id -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }
}
if (@(Get-Process leakcam-server -ErrorAction SilentlyContinue).Count -gt 0) { Say "FAIL: the server still runs"; exit 1 }
if ($New) {
    if (-not (Test-Path $New)) { Say "FAIL: no $New"; exit 1 }
    Copy-Item -Force $exe "$exe.old"
    Copy-Item -Force $New $exe
    Say "new exe $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16).ToLower()) from $New"
}
Start-ScheduledTask -TaskName 'LEAKCAM server'
for ($i = 1; $i -le 20; $i++) {
    Start-Sleep 1
    try {
        $r = Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 'http://127.0.0.1:8090/'
        $p = Get-Process leakcam-server
        Say "OK: answers $($r.StatusCode) after $i s, pid $($p.Id), exe $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16).ToLower())"
        exit 0
    } catch { }
}
Say "FAIL: no answer on 8090 within 20 s (task $((Get-ScheduledTask -TaskName 'LEAKCAM server').State))"
exit 1
