# The LEAKCAM server's autostart and firewall on the Windows PC (run once from an administrator PowerShell: UAC is on,
# so nothing started remotely can do it):
# the scheduled task "LEAKCAM server" starts start.bat at boot as SYSTEM (no login needed) and restarts it if it ends;
# the firewall opens 8090 (the app's HTTP) and 8443 (the boards' mTLS).
$d = 'C:\hw\leakcam-server'
$a = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c `"$d\start.bat`""
$t = New-ScheduledTaskTrigger -AtStartup
$s = New-ScheduledTaskSettingsSet -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit 0 `
     -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
$p = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName 'LEAKCAM server' -Action $a -Trigger $t -Settings $s -Principal $p -Force | Out-Null
foreach ($port in 8090, 8443) {
    $n = "LEAKCAM server $port"
    if (-not (Get-NetFirewallRule -DisplayName $n -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName $n -Direction Inbound -Protocol TCP -LocalPort $port -Action Allow -Profile Any | Out-Null
    }
}
# Go for building the server here (user 2026-11-12: "install go sdk latest"): the official MSI the PC downloaded and
# checked (SHA-256 against go.dev) into %TEMP%, installed quietly when it is there and Go is not
$msi = Join-Path $env:TEMP 'go1.27.4.windows-amd64.msi'
if ((Test-Path $msi) -and -not (Test-Path 'C:\Program Files\Go\bin\go.exe')) {
    Start-Process msiexec.exe -ArgumentList "/i `"$msi`" /qn /norestart" -Wait
}
# the clock (ntp_sync.ps1): at boot and every hour, as SYSTEM
$na = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$d\ntp_sync.ps1`""
$nt = @((New-ScheduledTaskTrigger -AtStartup), (New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Hours 1)))
Register-ScheduledTask -TaskName 'NTP sync' -Action $na -Trigger $nt -Principal $p -Force | Out-Null
Start-ScheduledTask -TaskName 'NTP sync'
Start-ScheduledTask -TaskName 'LEAKCAM server'
Get-ScheduledTask -TaskName 'LEAKCAM server' | Select-Object TaskName, State
