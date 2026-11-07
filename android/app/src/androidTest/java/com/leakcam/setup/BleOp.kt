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
 *   op scan     secs: first advertisement with the service UUID: name (the serial), address, rssi, ms
 *   op unpair   address: the phone's bond removed
 *   op session  address, pair (accept | cancel | skip), pin (the board's 6 digits, from the server; without it a PIN
 *               dialog is cancelled: a phone without the app), mtu (247; 23 = no exchange: long values go as offset writes),
 *               writes: "char:hex,char:hex,..." (char ssid | psk | commit), each write's GATT status; then the STATUS
 *               notifications until a final state or secs; drop_at: disconnect on that state (hex, e.g. 01)
 *   op app      the setup app itself, as a user meets it (test106): opens it, Find, the network (ssid, psk: hex; empty =
 *               open), Pair, check and save, the pairing dialogs accepted; every help text with its time (the app
 *               shows the SSID, never the passphrase) until the buttons are back or secs; anyway=1: then Save
 *               without checking, where the app offers it. On the status screen (LEAKCAMs remembered) it waits for
 *               the LEAKCAM on USB power and opens its setup (setup_open) first
 *   op delete   id: on the status screen, pick that LEAKCAM, the three-dots menu, Delete, the confirmation (test111)
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
    /* the arguments come in a JSON file (argsfile, pushed by tools/test-runner/phone.py and removed after the call):
     * on the command line adbd logged them, the Wi-Fi passphrase in hex included, into the phone's logcat (2026-10-30) */
    private val fileArgs = args.getString("argsfile")?.let { JSONObject(java.io.File(it).readText()) } ?: JSONObject()
    private fun arg(k: String, d: String) = fileArgs.optString(k, args.getString(k) ?: d)

    @Test
    fun run() {
        try {
            when (val op = arg("op", "")) {
                "scan" -> scan()
                "unpair" -> unpair()
                "session" -> session()
                "app" -> app()
                "delete" -> delete()
                else -> out.put("error", "unknown op '$op'")
            }
        } catch (e: Exception) {
            out.put("error", e.toString())
        }
        out.put("ms_total", ms())
        out.put("t0_epoch_ms", System.currentTimeMillis() - ms())   /* the phone's clock at t0: timelines with the board */
        inst.sendStatus(0, Bundle().apply { putString("result", out.toString()) })
    }

    private fun scan() {
        val q = LinkedBlockingQueue<ScanResult>()
        val cb = object : ScanCallback() {
            override fun onScanResult(type: Int, r: ScanResult) {
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

    private fun delete() {
        val ui = UiDevice.getInstance(inst)
        val pkg = ctx.packageName
        ctx.startActivity(ctx.packageManager.getLaunchIntentForPackage(pkg)!!.addFlags(
            android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_CLEAR_TASK))
        // Delete is a rare action: pick the board, then the three-dots menu next to the action button (2026-11-07)
        val pick = ui.wait(Until.findObject(By.desc("pick ${arg("id", "")}")), 10000)
        out.put("found", pick != null)
        pick?.click() ?: return
        SystemClock.sleep(300)
        val more = ui.wait(Until.findObject(By.descStartsWith(ctx.getString(R.string.more))), 5000)   // its state after it
        out.put("more", more != null)
        more?.click() ?: return
        val item = ui.wait(Until.findObject(By.text(ctx.getString(R.string.delete))), 5000)
        out.put("menu", item != null)
        item?.click() ?: return
        val ok = ui.wait(Until.findObject(By.res("android", "button1")), 5000)
        out.put("confirmed", ok != null)
        ok?.click()
        SystemClock.sleep(1000)
    }

    private fun app() {
        val ui = UiDevice.getInstance(inst)
        val pkg = ctx.packageName
        ctx.startActivity(ctx.packageManager.getLaunchIntentForPackage(pkg)!!.addFlags(
            android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_CLEAR_TASK))
        val texts = JSONArray()
        var last = ""
        /* the app redraws while it is read: a stale object is retried on the next poll, never fatal (three setups
         * ended on one, test106 2026-10-30) */
        fun <T> safe(d: T, f: () -> T): T = try { f() } catch (e: androidx.test.uiautomator.StaleObjectException) { d }
        fun help() = safe("") { ui.findObject(By.res(pkg, "help"))?.text ?: "" }
        fun note() { val h = help(); if (h != last && h.isNotEmpty()) { last = h; texts.put(JSONObject().put("ms", ms()).put("help", h)) } }
        /* the screen scrolls: a view off it (Save without checking under a long message) is not in the UI tree until
         * scrolled to, and a tap during the scroll got lost (test106, 2026-10-30) */
        fun find(id: String): androidx.test.uiautomator.UiObject2? = safe(null) {
            ui.findObject(By.res(pkg, id)) ?: ui.findObject(By.scrollable(true))?.let { s ->
                for (dir in listOf(androidx.test.uiautomator.Direction.DOWN, androidx.test.uiautomator.Direction.UP))
                    if (s.scrollUntil(dir, Until.findObject(By.res(pkg, id))) != null) break
                ui.waitForIdle()
                ui.findObject(By.res(pkg, id))
            }
        }
        fun enabled(id: String) = safe(false) { find(id)?.isEnabled == true }
        var usb = false
        /* the cable screen has no Find: looking for it scrolled the screen up and down until the 150 s timeout, the
         * setup long Done (test113, 2026-11-06) */
        fun busy() = !enabled("send") || (!usb && !enabled("scan"))
        /* the status screen first when the app remembers LEAKCAMs: its button opens the setup once the board on USB
         * power is found (test111, 2026-11-01) */
        val shown = SystemClock.elapsedRealtime() + 5000
        while (SystemClock.elapsedRealtime() < shown && ui.findObject(By.res(pkg, "status_panel")) == null &&
               ui.findObject(By.res(pkg, "send")) == null)
            SystemClock.sleep(100)
        if (ui.findObject(By.res(pkg, "status_panel")) != null) {
            out.put("status_screen", true)
            /* UiAutomator's own wait for the enabled button: the scrolling find() lost it for 40 s while the list
             * redrew (test106, 2026-11-01) */
            /* the setup itself, never Wake (a present board out of range: test119 taps that one itself) */
            /* tapped again until the setup screen opens: a tap while the list was still picking the board opened
             * nothing (test111, 2026-11-04: 3 runs left on the status screen) */
            /* a new board: its Set up button; one of mine: the three-dots menu's Change Wi-Fi (2026-11-07: photo and video
             * are the row's buttons, Change Wi-Fi a rare action), never its Wake */
            var woken = false
            val menuSeen = mutableListOf<String>()
            val deadline = SystemClock.elapsedRealtime() + (arg("secs", "60").toLong() - 5).coerceAtLeast(10) * 1000
            for (attempt in 1..3) {
                val change = ctx.getString(R.string.setup_change)
                val wakeLabel = ctx.getString(R.string.setup_wake)
                // one deadline for all tries, inside the op's own time (its result is lost past the runner's limit)
                var opened = false
                while (!opened && SystemClock.elapsedRealtime() < deadline) {
                    val setup = safe(null) { ui.findObject(By.res(pkg, "setup_open").enabled(true)) }
                    if (setup != null) {
                        safe(Unit) { setup.click() }
                        opened = true
                    } else if (safe(null) { ui.findObject(By.res(pkg, "take_photo")) } != null) {
                        // out of BLE range the item is Wake: tapped once (the board opens BLE at its next check-in),
                        // then Change Wi-Fi once the app sees it; the menu opened only when the three dots say its
                        // item works now (a disabled popup item reads as enabled)
                        val desc = safe("") { ui.findObject(By.res(pkg, "more"))?.contentDescription ?: "" }
                        menuSeen.add(desc)
                        if (desc.endsWith(ctx.getString(R.string.not_now)) || (woken && !desc.endsWith(change))) {
                            SystemClock.sleep(500)
                            continue
                        }
                        safe(Unit) { ui.findObject(By.res(pkg, "more"))?.click() }
                        val item = ui.wait(Until.findObject(By.text(java.util.regex.Pattern.compile(
                            java.util.regex.Pattern.quote(change) + "|" + java.util.regex.Pattern.quote(wakeLabel))).enabled(true)), 2000)
                        val isChange = safe(false) { item?.text == change }
                        if (item == null) ui.pressBack()
                        else if (isChange) { safe(Unit) { item.click() }; opened = true }
                        else if (!woken) { safe(Unit) { item.click() }; woken = true; out.put("ms_wake", ms()) }
                        else ui.pressBack()
                    }
                    if (!opened) SystemClock.sleep(500)
                }
                out.put("ms_status", ms())
                out.put("menu_seen", menuSeen.distinct().joinToString(" | ").take(400))
                if (ui.wait(Until.findObject(By.res(pkg, "send")), 5000) != null)
                    break
                out.put("setup_open_taps", attempt + 1)
            }
        }
        /* from the status screen the board is picked already: no second Find, as a user would not tap it (a second
         * scan right after a setup missed the board once, test106 2026-11-01) */
        /* the cable screen (a LEAKCAM on the USB cable, test113) has no Find: the board is found by the plug */
        usb = safe(false) { ui.findObject(By.res(pkg, "usb_header"))?.let { it.text.isNotEmpty() } == true }
        out.put("usb_screen", usb)
        if (!out.optBoolean("status_screen") && !usb)
            safe(Unit) { ui.wait(Until.findObject(By.res(pkg, "scan")), 10000)?.click() }
        val found = SystemClock.elapsedRealtime() + 15000
        /* Send turns on at the first board (Find goes on behind it since test106's timing work) */
        while (SystemClock.elapsedRealtime() < found && !enabled("send")) { note(); SystemClock.sleep(100) }
        note()
        out.put("found", safe(0) { ui.findObject(By.res(pkg, "devices"))?.childCount ?: 0 })
        out.put("ms_found", ms())
        fun hex(k: String) = arg(k, "").chunked(2).map { it.toInt(16).toByte() }.toByteArray().toString(Charsets.UTF_8)
        /* no setup screen: say what was on the screen instead of a NullPointerException (test111, 2026-11-04) */
        val ssidField = find("ssid")
        val pskField = find("password")
        if (ssidField == null || pskField == null) {
            val shown = safe("") { ui.findObjects(By.pkg(pkg)).mapNotNull { it.text?.takeIf { t -> t.isNotBlank() } }
                .take(12).joinToString(" | ") }
            out.put("error", "no setup screen; on screen: $shown")
            return
        }
        ssidField.text = hex("ssid")
        pskField.text = hex("psk")
        /* no pressBack: setting the text opens no keyboard, and Back closed the app (2026-10-30) */
        val pressed = mutableListOf<String>()
        fun press(button: String) {
            /* the app answers a tap at once (buttons off or a new help text): else the tap was lost, tap again */
            for (attempt in 1..3) {
                val before = help()
                safe(Unit) { find(button)?.click() }
                val seen = SystemClock.elapsedRealtime() + 2000
                while (SystemClock.elapsedRealtime() < seen && !busy() && help() == before)
                    SystemClock.sleep(100)
                if (busy() || help() != before)
                    break
                out.put("taps_$button", attempt + 1)
            }
            val until = SystemClock.elapsedRealtime() + arg("secs", "150").toLong() * 1000
            var lastSeen = SystemClock.elapsedRealtime()
            while (SystemClock.elapsedRealtime() < until) {
                note()
                /* the phone sent the app to the background a second after pairing (Samsung, test106 2026-11-01): it
                 * went on to Done unseen; brought back to the front, not restarted */
                if (ui.findObject(By.pkg(pkg)) != null)
                    lastSeen = SystemClock.elapsedRealtime()
                else if (SystemClock.elapsedRealtime() - lastSeen > 3000 && ui.findObject(By.res("android", "button1")) == null) {
                    out.put("brought_back", ms())
                    ctx.startActivity(ctx.packageManager.getLaunchIntentForPackage(pkg)!!.addFlags(
                        android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_REORDER_TO_FRONT))
                    lastSeen = SystemClock.elapsedRealtime()
                }
                safe(Unit) {   /* the dialog can go between finding and pressing; a PIN dialog is the app's to fill */
                    if (ui.findObject(By.clazz("android.widget.EditText").pkg("com.android.settings")) == null)
                        ui.findObject(By.res("android", "button1"))?.let { pressed.add("'${it.text}'@${ms()}"); it.click(); SystemClock.sleep(500) }
                }
                if (!busy()) break   /* finishSend: the send is over */
                SystemClock.sleep(200)
            }
            note()
        }
        press("send")
        // Save without checking is a rare action: the setup screen's three-dots menu (2026-11-07)
        var item: androidx.test.uiautomator.UiObject2? = null
        // not find(): an invisible menu is not in the tree, and find() would scroll the whole screen looking for it
        val menuButton = safe(null) { ui.findObject(By.res(pkg, "setup_more")) }
        if (menuButton != null) {
            safe(Unit) { menuButton.click() }
            item = ui.wait(Until.findObject(By.text(ctx.getString(R.string.save_anyway))), 2000)
        }
        val offered = item != null
        out.put("save_anyway", offered)
        if (offered && arg("anyway", "") == "1") {
            out.put("ms_anyway", ms())
            safe(Unit) { item?.click() }
        } else if (item != null || ui.findObject(By.text(ctx.getString(R.string.to_status))) != null)
            ui.pressBack()   // the menu closed again

        /* hold: the screen watched this long after the send (the board's relay back on the cable after Done, test113
         * step 12): the op's end ends the app's process, and a plug then starts a new one */
        val held = SystemClock.elapsedRealtime() + arg("hold", "0").toLong() * 1000
        while (SystemClock.elapsedRealtime() < held) { note(); SystemClock.sleep(200) }
        out.put("pairing", pressed.joinToString("+"))
        out.put("help", texts)
        out.put("ms_end", ms())
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
            /* passkey entry (since 2026-11-01): the PIN typed, as the app enters it; no PIN: cancelled */
            val field = ui.findObject(By.clazz("android.widget.EditText").pkg("com.android.settings"))
            if (b != null && field != null && button == "button1") {
                val pin = arg("pin", "")
                if (pin.isEmpty()) {
                    ui.findObject(By.res("android", "button2"))?.click()
                    pressed.add("'no PIN: cancel'")
                } else {
                    field.text = pin
                    SystemClock.sleep(300)
                    b.click()
                    pressed.add("'PIN'")
                }
                SystemClock.sleep(500)
                continue
            }
            if (b != null) {
                pressed.add("'${b.text}'")
                b.click()
                SystemClock.sleep(500)
                continue
            }
            if (!shade && pressed.isEmpty() && SystemClock.elapsedRealtime() > until - 22000) {
                shade = true
                ui.openNotification()
                ui.wait(Until.findObject(By.text(Pattern.compile("(?i).*([0-9A-F]{12}|pair|сопряж|подключ).*"))), 2000)?.click()
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
