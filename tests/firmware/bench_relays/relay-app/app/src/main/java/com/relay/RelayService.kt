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
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.SystemClock
import android.util.Log
import java.util.UUID
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
 */
class RelayService : Service() {
    private val thread = HandlerThread("relay").apply { start() }
    private val work = Handler(thread.looper)
    private val events = LinkedBlockingQueue<Pair<String, String>>()   // (kind, value): connected, services, ccc, reply
    private var gatt: BluetoothGatt? = null
    private var char: BluetoothGattCharacteristic? = null
    private var address = ""

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

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        foreground()
        val id = intent?.getStringExtra("id") ?: return START_NOT_STICKY
        val addr = intent.getStringExtra("address") ?: "A4:06:E9:9D:30:7E"
        val cmds = intent.getStringExtra("cmds").orEmpty().split(",").filter { it.isNotBlank() }
        work.post { Log.i(TAG, "RESULT $id ${run(addr, cmds)}") }
        return START_NOT_STICKY
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
        work.post { drop() }
        thread.quitSafely()
        super.onDestroy()
    }

    companion object {
        const val TAG = "LeakcamRelay"
        val SERVICE: UUID = UUID.fromString("0000ffe0-0000-1000-8000-00805f9b34fb")
        val CHAR: UUID = UUID.fromString("0000ffe1-0000-1000-8000-00805f9b34fb")
        val CCC: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
