# The PC's clock from NTP, hourly and at boot (scheduled task "NTP sync", install.ps1). Windows' own time service
# sends from UDP port 123 and gets no answer on this network (the router or ISP drops it; an inbound firewall rule
# did not help, 2026-11-12), while a query from a random port works: so this asks time.windows.com (pool.ntp.org
# if not) itself and sets the clock when it is more than 1 s off. The PC's clock was 3 h 19 min behind (CMOS), which
# broke the LEAKCAM server's FCM login (Google rejects a token signed with a wrong time).
$log = 'C:\hw\leakcam-server\ntp_sync.log'
foreach ($server in 'time.windows.com', 'pool.ntp.org') {
    try {
        $udp = New-Object System.Net.Sockets.UdpClient
        $udp.Client.ReceiveTimeout = 3000
        $req = New-Object byte[] 48; $req[0] = 0x1B
        $t0 = [DateTime]::UtcNow
        [void]$udp.Send($req, 48, $server, 123)
        $ep = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
        $r = $udp.Receive([ref]$ep)
        $t1 = [DateTime]::UtcNow
        $udp.Close()
        $sec = ([uint64]$r[40] -shl 24) -bor ([uint64]$r[41] -shl 16) -bor ([uint64]$r[42] -shl 8) -bor [uint64]$r[43]
        $frac = ([uint64]$r[44] -shl 24) -bor ([uint64]$r[45] -shl 16) -bor ([uint64]$r[46] -shl 8) -bor [uint64]$r[47]
        $ntp = (New-Object DateTime 1900, 1, 1, 0, 0, 0, ([DateTimeKind]::Utc)).AddSeconds($sec + $frac / 4294967296.0)
        $now = $ntp.AddTicks(($t1 - $t0).Ticks / 2)          # the answer's time plus half the round trip
        $off = ($now - [DateTime]::UtcNow).TotalSeconds
        if ([Math]::Abs($off) -gt 1) { Set-Date -Date $now.ToLocalTime() | Out-Null }
        Add-Content $log ("{0:u} {1} offset {2:N2} s{3}" -f [DateTime]::UtcNow, $server, $off, $(if ([Math]::Abs($off) -gt 1) { ', set' } else { '' }))
        exit 0
    } catch {
        Add-Content $log ("{0:u} {1} failed: {2}" -f [DateTime]::UtcNow, $server, $_.Exception.Message)
    }
}
exit 1
