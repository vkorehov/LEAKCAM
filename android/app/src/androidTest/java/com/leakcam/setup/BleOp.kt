package com.leakcam.setup

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Bundle
import android.os.ParcelUuid
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.Until
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.regex.Pattern

/**
 * tests/test040 and test106: one BLE operation per call against a LEAKCAM board, the result as JSON in the
 * instrumentation status `result`. The runner (tests/test040-ble-wifi-credentials/run.py) chains the calls and checks
 * the board's console between them:
 *
 *   adb shell am instrument -w -r -e op <op> [-e key value ...] com.leakcam.setup.test/androidx.test.runner.AndroidJUnitRunner
 *
 *   op scan     name prefix (default LEAKCAM-), secs: first advertisement with the service UUID: name, address, rssi, ms
 *   op unpair   address: the phone's bond removed
 *   op session  address, pair (accept | cancel | skip), mtu (247; 23 = no exchange: long values go as offset writes),
 *               writes: "char:hex,char:hex,..." (char ssid | psk | commit), each write's GATT status; then the STATUS
 *               notifications until a final state or secs; drop_at: disconnect on that state (hex, e.g. 01)
 *
 * Values are hex so any length and byte reaches the board unchanged; the passphrase is never logged or returned.
 */
@SuppressLint("MissingPermission")
@RunWith(AndroidJUnit4::class)
class BleOp {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private val args: Bundle = InstrumentationRegistry.getArguments()
    private val ctx: Context = inst.targetContext
    private val bt = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    private val out = JSONObject()
    private val t0 = SystemClock.elapsedRealtime()

    private fun ms() = SystemClock.elapsedRealtime() - t0
    private fun arg(k: String, d: String) = args.getString(k) ?: d

    @Test
    fun run() {
        try {
            when (val op = arg("op", "")) {
                "scan" -> scan()
                "unpair" -> unpair()
                "session" -> session()
                else -> out.put("error", "unknown op '$op'")
            }
        } catch (e: Exception) {
            out.put("error", e.toString())
        }
        out.put("ms_total", ms())
        inst.sendStatus(0, Bundle().apply { putString("result", out.toString()) })
    }

    private fun scan() {
        val prefix = arg("prefix", "LEAKCAM-")
        val q = LinkedBlockingQueue<ScanResult>()
        val cb = object : ScanCallback() {
            override fun onScanResult(type: Int, r: ScanResult) {
                if (r.device.name?.startsWith(prefix) == true || r.scanRecord?.deviceName?.startsWith(prefix) == true)
                    q.offer(r)
            }
        }
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        bt.bluetoothLeScanner.startScan(listOf(filter), settings, cb)
        val r = q.poll(arg("secs", "10").toLong(), TimeUnit.SECONDS)
        bt.bluetoothLeScanner.stopScan(cb)
        out.put("found", r != null)
        if (r != null) {
            out.put("name", r.scanRecord?.deviceName ?: r.device.name)
            out.put("address", r.device.address)
            out.put("rssi", r.rssi)
            out.put("ms", ms())
        }
    }

    private fun unpair() {
        val d = bt.getRemoteDevice(arg("address", ""))
        if (d.bondState != BluetoothDevice.BOND_NONE)
            d.javaClass.getMethod("removeBond").invoke(d)    /* no public API; the runner needs a clean phone */
        val until = SystemClock.elapsedRealtime() + 5000
        while (d.bondState != BluetoothDevice.BOND_NONE && SystemClock.elapsedRealtime() < until)
            SystemClock.sleep(100)
        out.put("bonded", d.bondState != BluetoothDevice.BOND_NONE)
    }

    /* every callback as an event on one queue: the session reads them in order */
    private data class Ev(val kind: String, val status: Int = 0, val value: ByteArray? = null, val uuid: UUID? = null)

    private fun session() {
        val d = bt.getRemoteDevice(arg("address", ""))
        val q = LinkedBlockingQueue<Ev>()
        val cb = object : BluetoothGattCallback() {
            override fun onConnectionStateChange(g: BluetoothGatt, status: Int, state: Int) {
                q.offer(Ev(if (state == BluetoothProfile.STATE_CONNECTED) "connected" else "disconnected", status))
            }
            override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) { q.offer(Ev("mtu", mtu)) }
            override fun onServicesDiscovered(g: BluetoothGatt, status: Int) { q.offer(Ev("services", status)) }
            override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
                q.offer(Ev("write", status, uuid = c.uuid))
            }
            override fun onDescriptorWrite(g: BluetoothGatt, dsc: BluetoothGattDescriptor, status: Int) { q.offer(Ev("ccc", status)) }
            override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
                q.offer(Ev("status", value = value))
            }
            @Deprecated("API < 33")
            override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
                if (Build.VERSION.SDK_INT < 33) q.offer(Ev("status", value = c.value))
            }
        }
        /* STATUS notifications that arrive while waiting for something else are kept, not dropped: the board
         * notifies SAVED inside the commit write, before its write response (tests/test040, 2026-10-28) */
        val early = ArrayDeque<Ev>()
        fun next(kind: String, secs: Long): Ev? {
            if (kind == "status" && early.isNotEmpty()) return early.removeFirst()
            val until = SystemClock.elapsedRealtime() + secs * 1000
            while (true) {
                val left = until - SystemClock.elapsedRealtime()
                val e = q.poll(maxOf(left, 0), TimeUnit.MILLISECONDS) ?: return null
                if (e.kind == kind) return e
                if (e.kind == "status") early.addLast(e)
                if (e.kind == "disconnected") { out.put("disconnected_ms", ms()); return null }
            }
        }
        val gatt = d.connectGatt(ctx, false, cb, BluetoothDevice.TRANSPORT_LE)
        try {
            if (next("connected", 15) == null) { out.put("error", "no connection"); return }
            out.put("connect_ms", ms())
            when (arg("pair", "accept")) {      /* the board asks for encryption on connect: the system dialog */
                "accept" -> out.put("pair", pairDialog(d, "button1"))
                "cancel" -> out.put("pair", pairDialog(d, "button2"))
            }
            out.put("bond", d.bondState)
            out.put("pair_ms", ms())
            val mtu = arg("mtu", "247").toInt()
            if (mtu > 23) {
                gatt.requestMtu(mtu)
                out.put("mtu", next("mtu", 5)?.status ?: -1)
            }
            gatt.discoverServices()
            if (next("services", 10) == null) { out.put("error", "no services"); return }
            val svc = gatt.getService(SERVICE) ?: run { out.put("error", "no LEAKCAM service"); return }
            val st = svc.getCharacteristic(STATUS)
            gatt.setCharacteristicNotification(st, true)
            val ccc = st.getDescriptor(CCC)
            if (Build.VERSION.SDK_INT >= 33)
                gatt.writeDescriptor(ccc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            else {
                @Suppress("DEPRECATION") ccc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION") gatt.writeDescriptor(ccc)
            }
            out.put("ccc", next("ccc", 5)?.status ?: -1)
            val writes = JSONArray()
            for (w in arg("writes", "").split(",").filter { it.isNotBlank() }) {
                val (name, hex) = w.split(":", limit = 2)
                val uuid = mapOf("ssid" to SSID, "psk" to PSK, "commit" to COMMIT)[name] ?: error("char $name")
                val c = svc.getCharacteristic(uuid)
                val v = hex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
                val ok = if (Build.VERSION.SDK_INT >= 33)
                    gatt.writeCharacteristic(c, v, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS
                else {
                    @Suppress("DEPRECATION") c.value = v
                    @Suppress("DEPRECATION") gatt.writeCharacteristic(c)
                }
                val e = if (ok) next("write", 10) else null
                writes.put(JSONObject().put("char", name).put("len", v.size).put("status", e?.status ?: -1).put("ms", ms()))
            }
            out.put("writes", writes)
            /* the STATUS notifications: progress, then a final state (saved or a failure); drop_at cuts the link */
            val drop = args.getString("drop_at")?.toInt(16)
            val states = JSONArray()
            val until = SystemClock.elapsedRealtime() + arg("secs", "90").toLong() * 1000
            while (SystemClock.elapsedRealtime() < until) {
                val e = next("status", maxOf((until - SystemClock.elapsedRealtime()) / 1000, 1)) ?: break
                val s = e.value!![0].toInt() and 0xff
                val detail = if (e.value.size >= 3) (e.value[1].toInt() and 0xff) or ((e.value[2].toInt() and 0xff) shl 8) else 0
                states.put(JSONObject().put("state", s).put("detail", detail).put("ms", ms()))
                if (s == drop) { gatt.disconnect(); out.put("dropped_at", s); break }
                if (s >= 0x04) break                  /* SAVED or a failure: final */
            }
            out.put("states", states)
        } finally {
            gatt.disconnect()
            gatt.close()
        }
    }

    /* the system pairing dialog: press android:id/button1 (pair) or button2 (cancel) - by id, not by text: the
     * user's phone runs a Russian UI and the English texts never matched (tests/test040, 2026-10-28). Android shows
     * a notification instead of the dialog when the request comes while another app is in front: tap it first. */
    private fun pairDialog(d: BluetoothDevice, button: String): String {
        val ui = UiDevice.getInstance(inst)
        /* Samsung asks twice for one Secure Connections pairing: press every dialog until the bond is there or gone;
         * returning after the first press left the second open and the board's SMP timed out (tests/test040) */
        val until = SystemClock.elapsedRealtime() + 25000
        var shade = false
        val pressed = mutableListOf<String>()
        while (SystemClock.elapsedRealtime() < until) {
            if (d.bondState == BluetoothDevice.BOND_BONDED) return "bonded after ${pressed.joinToString("+")}"
            if (pressed.isNotEmpty() && d.bondState == BluetoothDevice.BOND_NONE) {
                SystemClock.sleep(1500)         /* cancelled or failed: give the board its disconnect */
                return "not bonded after ${pressed.joinToString("+")}"
            }
            val b = ui.wait(Until.findObject(By.res("android", button)), 1000)
            if (b != null) {
                pressed.add("'${b.text}'")
                b.click()
                SystemClock.sleep(500)
                continue
            }
            if (!shade && pressed.isEmpty() && SystemClock.elapsedRealtime() > until - 22000) {
                shade = true
                ui.openNotification()
                ui.wait(Until.findObject(By.text(Pattern.compile("(?i).*(LEAKCAM|pair|сопряж|подключ).*"))), 2000)?.click()
                    ?: ui.pressBack()
            }
        }
        return if (pressed.isEmpty()) "no dialog" else "still bonding after ${pressed.joinToString("+")}"
    }

    companion object {
        val SERVICE: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000001")
        val SSID: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000002")
        val PSK: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000003")
        val COMMIT: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000004")
        val STATUS: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000005")
        val CCC: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
