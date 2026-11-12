package com.relay

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.database.ContentObserver
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.provider.Settings
import java.net.Inet4Address
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.SecureRandom
import java.util.UUID
import java.util.concurrent.CompletableFuture
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * The bench relay box (HM-10 "LCRELAY", HMSoft V709; skill leakcam-bench-relays) driven from the PC through the phone,
 * apart from the product app: the product's test driver ran each switch as an instrumentation of the app, and Android
 * force-stopped the app at every run's end (2026-11-09: a test watching the app lost it at every unplug).
 *
 * One Bluetooth connection is kept open between requests (a switch ~0.3 s instead of ~4 s with a connect each). A
 * request:
 *   adb shell am start-foreground-service -n com.relay/.RelayService --es id <id> --es address <mac> \
 *       --es cmds "AT+BEFC3C0,AT+MPIO080,wait:1000,AT+MPIO3C0"
 * runs the commands in order (wait:<ms> pauses; every AT command waits up to 3 s for the box's answer) and logs
 *   RESULT <id> {"replies":[{"cmd":..,"reply":..,"ms":..},..],"connect_ms":..,"error":..}
 * under the tag LeakcamRelay (the PC reads it with logcat). Requests run one at a time, in their order.
 *
 * Remote (user, 2026-11-10: "add remote possibility to relay app"): the same requests over HTTP on the LAN, port
 * 8765, while the service runs - no adb needed (adb over Wi-Fi drops, its port changes):
 *   POST /run   body: the cmds list as above      -> the RESULT JSON
 *   GET  /state                                    -> {"connected":..,"address":..}
 *   GET  /adb                                      -> {"ip":..,"port":..,"enabled":..}: Wireless debugging switched
 *        on (Settings.Global adb_wifi_enabled; needs `pm grant com.relay android.permission.WRITE_SECURE_SETTINGS`,
 *        once over adb) and its port, found by the phone's own mDNS announcement (_adb-tls-connect._tcp) - the port
 *        changes at every toggle or restart (user, 2026-11-11: "build a relay app to enable debugger and tell port")
 * BootReceiver starts the service at boot: the remote port is back after a phone restart.
 * Wireless debugging is kept on (user, 2026-11-11: "relay app can also re-enable debugger"): at the service's start,
 * whenever the setting goes off (an observer on adb_wifi_enabled: Samsung switches it off by itself, e.g. at a Wi-Fi
 * reconnect) and whenever Wi-Fi comes back.
 * Every request carries the header "X-Token: <token>": the app makes the token once (files/token, 32 hex digits);
 * the PC reads it with `adb shell run-as com.relay cat files/token` (debug build) into ~/.leakcam_relay_token.
 */
class RelayService : Service() {
    private val thread = HandlerThread("relay").apply { start() }
    private val work = Handler(thread.looper)
    private val events = LinkedBlockingQueue<Pair<String, String>>()   // (kind, value): connected, services, ccc, reply
    private var gatt: BluetoothGatt? = null
    private var char: BluetoothGattCharacteristic? = null
    private var address = ""
    private var server: ServerSocket? = null
    private val token by lazy {
        val f = File(filesDir, "token")
        if (!f.exists()) {
            val b = ByteArray(16).also { SecureRandom().nextBytes(it) }
            f.writeText(b.joinToString("") { "%02x".format(it) })
        }
        f.readText().trim()
    }

    private val cb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, state: Int) {
            if (state == BluetoothProfile.STATE_CONNECTED) {
                events.offer("connected" to "")
            } else {
                events.offer("disconnected" to "$status")
                work.post { drop() }
            }
        }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) { events.offer("services" to "$status") }
        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            events.offer("ccc" to "$status")
        }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            events.offer("reply" to String(value))
        }
        @Deprecated("API < 33")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            if (Build.VERSION.SDK_INT < 33) events.offer("reply" to String(c.value))
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private var watching = false

    /** Wireless debugging on now and kept on (needs WRITE_SECURE_SETTINGS, granted once over adb) */
    private fun keepAdbOn() {
        val on = Runnable {
            try {
                if (Settings.Global.getInt(contentResolver, "adb_wifi_enabled", 0) != 1) {
                    Settings.Global.putInt(contentResolver, "adb_wifi_enabled", 1)
                    Log.i(TAG, "adb: Wireless debugging switched back on")
                }
            } catch (e: SecurityException) {
                Log.w(TAG, "adb: no WRITE_SECURE_SETTINGS: Wireless debugging not kept on")
            }
        }
        on.run()
        if (watching) return
        watching = true
        contentResolver.registerContentObserver(Settings.Global.getUriFor("adb_wifi_enabled"), false,
            object : ContentObserver(work) { override fun onChange(self: Boolean) { work.postDelayed(on, 1000) } })
        getSystemService(ConnectivityManager::class.java).registerNetworkCallback(
            NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build(),
            object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(n: Network) { work.postDelayed(on, 3000) }
            })
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        foreground()
        listen()
        keepAdbOn()
        val id = intent?.getStringExtra("id") ?: return START_STICKY
        val addr = intent.getStringExtra("address") ?: DEFAULT_ADDR
        val cmds = intent.getStringExtra("cmds").orEmpty().split(",").filter { it.isNotBlank() }
        work.post { Log.i(TAG, "RESULT $id ${run(addr, cmds)}") }
        return START_STICKY                    // restarted if Android stops it: the remote port stays
    }

    /** the HTTP listener on the LAN (one request at a time, each queued behind the adb ones on `work`) */
    private fun listen() {
        if (server != null) return
        val ss = try {
            ServerSocket().apply { reuseAddress = true; bind(InetSocketAddress(PORT)) }
        } catch (e: Exception) {
            Log.w(TAG, "remote: port $PORT: $e"); return
        }
        server = ss
        Thread({
            while (!ss.isClosed) {
                val c = try { ss.accept() } catch (e: Exception) { break }
                try { serve(c) } catch (e: Exception) { Log.w(TAG, "remote: $e") } finally { c.close() }
            }
        }, "relay-http").start()
        Log.i(TAG, "remote: listening on $PORT")
    }

    private fun serve(c: Socket) {
        c.soTimeout = 10000
        val inp = c.getInputStream().bufferedReader()
        val start = inp.readLine() ?: return
        val headers = HashMap<String, String>()
        while (true) {
            val l = inp.readLine() ?: break
            if (l.isEmpty()) break
            val i = l.indexOf(':')
            if (i > 0) headers[l.substring(0, i).trim().lowercase()] = l.substring(i + 1).trim()
        }
        val len = headers["content-length"]?.toIntOrNull() ?: 0
        val body = if (len in 1..4096) CharArray(len).also { var n = 0; while (n < len) { val k = inp.read(it, n, len - n); if (k < 0) break; n += k } }.concatToString() else ""
        val (method, path) = start.split(" ").let { (it.getOrNull(0) ?: "") to (it.getOrNull(1) ?: "") }
        val (code, out) = when {
            headers["x-token"] != token -> 403 to "{\"error\":\"token\"}"
            method == "GET" && path == "/adb" -> 200 to adb()
            method == "GET" && path == "/state" -> 200 to "{\"connected\":${char != null},\"address\":${json(address)}}"
            method == "POST" && path.startsWith("/run") -> {
                val addr = Regex("[?&]address=([0-9A-Fa-f:]{17})").find(path)?.groupValues?.get(1) ?: DEFAULT_ADDR
                val cmds = body.trim().split(",").filter { it.isNotBlank() }
                val f = CompletableFuture<String>()
                work.post { f.complete(run(addr, cmds)) }
                200 to f.get(60, TimeUnit.SECONDS)
            }
            else -> 404 to "{\"error\":\"not found\"}"
        }
        val b = out.toByteArray()
        c.getOutputStream().apply {
            write(("HTTP/1.1 $code ${if (code == 200) "OK" else "Error"}\r\nContent-Type: application/json\r\n" +
                "Content-Length: ${b.size}\r\nConnection: close\r\n\r\n").toByteArray())
            write(b)
            flush()
        }
    }

    /** Wireless debugging on, then its live port: the phone's own mDNS announcement, kept only when a TCP connect to it
     * works (the cache handed back a stale port after a re-enable, 2026-11-11: "connection refused"); none in 8 s ->
     * Wireless debugging switched off and on once and looked for again */
    private fun adb(): String {
        val first = adbOnce()
        if (!first.contains("\"port\":-1")) return first
        try {
            Settings.Global.putInt(contentResolver, "adb_wifi_enabled", 0)
            SystemClock.sleep(2000)
            Settings.Global.putInt(contentResolver, "adb_wifi_enabled", 1)
            SystemClock.sleep(2000)
        } catch (e: SecurityException) {
            return first
        }
        return adbOnce()
    }

    private fun alive(ip: String, port: Int) = try {
        Socket().use { it.connect(InetSocketAddress(ip, port), 700) }
        true
    } catch (e: Exception) {
        false
    }

    private fun adbOnce(): String {
        var enabled = Settings.Global.getInt(contentResolver, "adb_wifi_enabled", 0) == 1
        if (!enabled) {
            enabled = try {
                Settings.Global.putInt(contentResolver, "adb_wifi_enabled", 1)
            } catch (e: SecurityException) {
                return "{\"error\":\"no WRITE_SECURE_SETTINGS (adb shell pm grant com.relay android.permission.WRITE_SECURE_SETTINGS)\"}"
            }
        }
        val ip = ownIp() ?: return "{\"error\":\"no Wi-Fi address\",\"enabled\":$enabled}"
        val nsd = getSystemService(NsdManager::class.java)
        val found = CompletableFuture<Int>()
        val pending = LinkedBlockingQueue<NsdServiceInfo>()
        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(t: String) {}
            override fun onDiscoveryStopped(t: String) {}
            override fun onStartDiscoveryFailed(t: String, e: Int) { found.complete(-1) }
            override fun onStopDiscoveryFailed(t: String, e: Int) {}
            override fun onServiceFound(i: NsdServiceInfo) { pending.offer(i) }
            override fun onServiceLost(i: NsdServiceInfo) {}
        }
        nsd.discoverServices("_adb-tls-connect._tcp", NsdManager.PROTOCOL_DNS_SD, listener)
        val until = SystemClock.elapsedRealtime() + 8000
        while (!found.isDone && SystemClock.elapsedRealtime() < until) {
            val i = pending.poll(200, TimeUnit.MILLISECONDS) ?: continue
            val r = CompletableFuture<NsdServiceInfo?>()
            @Suppress("DEPRECATION")
            nsd.resolveService(i, object : NsdManager.ResolveListener {
                override fun onResolveFailed(x: NsdServiceInfo, e: Int) { r.complete(null) }
                override fun onServiceResolved(x: NsdServiceInfo) { r.complete(x) }
            })
            val x = try { r.get(3, TimeUnit.SECONDS) } catch (e: Exception) { null } ?: continue
            @Suppress("DEPRECATION")
            if (x.host?.hostAddress == ip && alive(ip, x.port))   // this phone's, not another's; listening now
                found.complete(x.port)
        }
        try { nsd.stopServiceDiscovery(listener) } catch (_: Exception) {}
        val port = if (found.isDone) found.get() else -1
        return "{\"ip\":${json(ip)},\"port\":$port,\"enabled\":$enabled}"
    }

    private fun ownIp(): String? {
        val cm = getSystemService(ConnectivityManager::class.java)
        val lp = cm.getLinkProperties(cm.activeNetwork) ?: return null
        return lp.linkAddresses.map { it.address }.firstOrNull { it is Inet4Address }?.hostAddress
    }

    private fun foreground() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel("relay", "Relay box", NotificationManager.IMPORTANCE_MIN))
        val n = Notification.Builder(this, "relay").setContentTitle("Relay box connected")
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth).build()
        if (Build.VERSION.SDK_INT >= 29)
            startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        else
            startForeground(1, n)
    }

    private fun json(s: String) = "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""

    /** the commands on the open connection (connected first when needed); the result as JSON */
    private fun run(addr: String, cmds: List<String>): String {
        val t0 = SystemClock.elapsedRealtime()
        val ms = { SystemClock.elapsedRealtime() - t0 }
        if (addr != address)
            drop()
        var connectMs = 0L
        if (char == null) {
            val err = connect(addr)
            if (err != null)
                return "{\"error\":${json(err)},\"replies\":[]}"
            connectMs = ms()
        }
        val replies = StringBuilder()
        for (cmd in cmds) {
            if (cmd.startsWith("wait:")) {
                SystemClock.sleep(cmd.removePrefix("wait:").toLongOrNull() ?: 0)
                continue
            }
            events.clear()
            if (!write(cmd)) {
                drop()
                return "{\"error\":\"write failed\",\"connect_ms\":$connectMs,\"replies\":[${replies}]}"
            }
            val reply = next("reply", 3000)
            if (replies.isNotEmpty()) replies.append(",")
            replies.append("{\"cmd\":${json(cmd)},\"reply\":${json(reply ?: "")},\"ms\":${ms()}}")
        }
        return "{\"connect_ms\":$connectMs,\"replies\":[${replies}]}"
    }

    private fun next(kind: String, timeoutMs: Long): String? {
        val until = SystemClock.elapsedRealtime() + timeoutMs
        while (true) {
            val e = events.poll(maxOf(until - SystemClock.elapsedRealtime(), 0), TimeUnit.MILLISECONDS) ?: return null
            if (e.first == kind) return e.second
            if (e.first == "disconnected") return null
        }
    }

    @SuppressLint("MissingPermission")   // BLUETOOTH_CONNECT granted at install (adb install -g)
    private fun connect(addr: String): String? {
        val bt = getSystemService(BluetoothManager::class.java).adapter ?: return "no Bluetooth"
        events.clear()
        val g = bt.getRemoteDevice(addr).connectGatt(this, false, cb, BluetoothDevice.TRANSPORT_LE)
        gatt = g
        address = addr
        if (next("connected", 10000) == null) { drop(); return "no connection" }
        g.discoverServices()
        if (next("services", 10000) == null) { drop(); return "no services" }
        val c = g.getService(SERVICE)?.getCharacteristic(CHAR) ?: run { drop(); return "no FFE0/FFE1: not the relay box" }
        g.setCharacteristicNotification(c, true)
        val ccc = c.getDescriptor(CCC)
        if (Build.VERSION.SDK_INT >= 33)
            g.writeDescriptor(ccc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        else {
            @Suppress("DEPRECATION") ccc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION") g.writeDescriptor(ccc)
        }
        next("ccc", 5000)
        char = c
        return null
    }

    @SuppressLint("MissingPermission")
    private fun write(cmd: String): Boolean {
        val g = gatt ?: return false
        val c = char ?: return false
        val v = cmd.toByteArray()
        return if (Build.VERSION.SDK_INT >= 33)
            g.writeCharacteristic(c, v, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) == 0
        else {
            @Suppress("DEPRECATION") c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            @Suppress("DEPRECATION") c.value = v
            @Suppress("DEPRECATION") g.writeCharacteristic(c)
        }
    }

    @SuppressLint("MissingPermission")
    private fun drop() {
        gatt?.let { it.disconnect(); it.close() }
        gatt = null
        char = null
    }

    override fun onDestroy() {
        try { server?.close() } catch (_: Exception) {}
        work.post { drop() }
        thread.quitSafely()
        super.onDestroy()
    }

    companion object {
        const val TAG = "LeakcamRelay"
        const val PORT = 8765
        const val DEFAULT_ADDR = "A4:06:E9:9D:30:7E"
        val SERVICE: UUID = UUID.fromString("0000ffe0-0000-1000-8000-00805f9b34fb")
        val CHAR: UUID = UUID.fromString("0000ffe1-0000-1000-8000-00805f9b34fb")
        val CCC: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}

/** the service at boot and after an update: the remote port back after a phone restart, no adb needed (relay app README) */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(c: Context, i: Intent) {
        if (i.action == Intent.ACTION_BOOT_COMPLETED || i.action == Intent.ACTION_MY_PACKAGE_REPLACED)
            c.startForegroundService(Intent(c, RelayService::class.java))
    }
}
