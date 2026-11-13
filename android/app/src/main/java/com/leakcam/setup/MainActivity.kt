package com.leakcam.setup

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothAdapter
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
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import android.text.InputType
import android.util.Log
import android.view.View
import android.view.WindowInsets
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID

/**
 * Sets the Wi-Fi network of a LEAKCAM over BLE (firmware/bl616_setup/ble_pairing.h).
 *
 * The board advertises the provisioning service while it is on USB power. The app scans for that
 * service, connects, pairs (LE Secure Connections, Just Works: the board accepts pairing only on USB
 * power), subscribes to STATUS, then writes SSID, passphrase and commit. Commit 0x01 makes the
 * board join the network, get an address and reach the internet before it stores anything; STATUS
 * reports each step and the result (firmware/bl616_common/wifi_check_calc.h). STATUS is the board's state for the
 * commit's attempt id: read once a second as well, so a lost notification only delays the answer; after a lost
 * link (or Bluetooth off) the app reconnects and reads it, and never claims what the board did without it
 * (tests/test110). A failed connect is retried. Commit 0x02 stores without
 * checking, for a board set up out of reach of its network.
 *
 * The screen walks through four steps (plug in, find, Wi-Fi, pair and save); the box under them
 * says what to do now, and for pairing what the phone will show.
 *
 * More than one LEAKCAM (use case 2, tests/test111): the app opens on a status screen, this phone's LEAKCAMs online or
 * offline as the server sees them (BuildConfig.SERVER, GET /v1/phones/<Android ID>/devices, every 10 s). The server is
 * the one source of truth (DESIGN.md 5.3.5): the app stores no list; the server registers the phone as a board's user
 * when it issues the PIN, and a delete goes to the server (the board back in setup when no user is left). Setup opens for a LEAKCAM found nearby (a new one advertises a minute every 5 minutes
 * on its battery, one set up only on USB power) or when the list is empty. Each LEAKCAM advertises its device ID and a flags byte (scan response, manufacturer data,
 * company 0xFFFF: the BL616's factory MAC, bit 0 a stored network, bit 1 USB power, then the battery in 20 mV steps,
 * DESIGN.md 5.3.9): a known one gets the short "change its Wi-Fi"
 * flow. Pairing is LE Secure Connections passkey entry: the app gets the board's PIN from the server right before it
 * pairs and enters it itself; the app holds no secret, a phone without it cannot pair. The app never stores Wi-Fi
 * credentials. The SSID is not secret: each LEAKCAM reports its network with every
 * check-in and the server passes it on, so the list shows it and the change flow starts from it; the passphrase is
 * never shown (hidden when the board has one), typing replaces it. A deleted LEAKCAM never comes back by itself: only a
 * new setup brings it back.
 *
 * The USB cable first (DESIGN.md 5.3.4: the most reliable channel; tests/test113): a LEAKCAM plugged into the phone
 * (phone = USB host) on its setup firmware runs the K230's USB setup relay, and the app talks to it in lines
 * (UsbLink.kt; firmware/bl616_setup/usb_setup.c): ID, then PIN (the server's, as for BLE), SSID and PSK in hex, COMMIT,
 * and STATUS lines with the same states as the BLE characteristic. No pairing. That board shows with a USB icon, only
 * while the link is up (a plug alone stays the plug icon), and is set up over the cable, not over Bluetooth.
 * The cable flow (user 2026-11-05): the plug launches the app (default handler); it starts on a spinning logo while
 * the USB device enumerates and answers ID, then opens the cable screen: the board already found (no list, no steps),
 * the Wi-Fi fields open at once when it has no network. No LEAKCAM on the cable by then: the Bluetooth flow. In the
 * list the board on the cable is always the selected one, "connected by USB cable".
 */
@SuppressLint("MissingPermission") // every Bluetooth call runs after permissionsGranted()
/** the signal as 1-4 bars, everywhere the app shows it (the rows, the leak alert) */
fun barCount(dbm: Int) = when {
    dbm >= -60 -> 4
    dbm >= -70 -> 3
    dbm >= -80 -> 2
    else -> 1
}

class MainActivity : Activity() {

    private companion object {
        val SERVICE: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000001")
        val SSID: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000002")
        val PSK: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000003")
        val COMMIT: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000004")
        val STATUS: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000005")
        val CCC: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        const val COMMIT_CHECK: Byte = 1
        const val COMMIT_UNCHECKED: Byte = 2
        // STATUS states, as in firmware/bl616_common/wifi_check_calc.h
        const val WCHK_IDLE = 0x00
        const val WCHK_JOINING = 0x01
        const val WCHK_ADDRESS = 0x02
        const val WCHK_INTERNET = 0x03
        const val WCHK_SAVED = 0x04
        const val WCHK_NOT_FOUND = 0x10
        const val WCHK_WRONG_PASSWORD = 0x11
        const val WCHK_JOIN_FAILED = 0x12
        const val WCHK_NO_ADDRESS = 0x13
        const val WCHK_NO_DNS = 0x14
        const val WCHK_NO_INTERNET = 0x15
        const val WCHK_CAPTIVE = 0x16
        const val WCHK_BUSY = 0x17
        const val WCHK_SAVE_FAILED = 0x18
        const val WCHK_NO_API = 0x19     // the internet works, the LEAKCAM server does not answer (detail: HTTP status)
        const val TAG = "LeakcamSetup"
        const val MTU = 247              // the longest value, a 63-byte passphrase, in one write
        const val SCAN_MS = 10_000L           // no board by then: the setup screen says how to bring one
        const val SEND_TIMEOUT_MS = 60_000L   // per step: pairing (the user reads and accepts), each check step
        const val POLL_MS = 1_000L            // STATUS read while the check runs: a notification can be lost (test106)
        const val SWAP_DB = 6                 // a new board this much stronger replaces the shown one (one setup at a time)
        const val CONNECT_TRIES = 3           // connects before a failure is shown: Android's GATT 133 is common (test110)
        const val RETRY_MS = 500L
        const val RECONCILE_MS = 20_000L      // reconnecting after a lost link to read what the board did
        const val REQUEST_PERMISSIONS = 1
        const val REQUEST_ENABLE_BT = 2
        const val REQUEST_NOTIFY = 3
        const val STEP_PLUG = 1
        const val STEP_FIND = 2
        const val STEP_WIFI = 3
        const val STEP_SAVE = 4
        const val STEP_DONE = 5
        const val UNPAIR_MS = 5_000L          // the phone removes an old bond
        const val USB_TAG = "usb"             // the board on the USB cable, in the lists where a BLE address would be
        const val USB_ID_TRIES = 10           // ID asked once a second until the K230's relay answers
        // the periods; a debug build on the bench runs them 3x faster (`adb shell settings put global
        // leakcam_fast_timing 1`): the tests wait for the app's refreshes, not real-time (2026-11-07)
        const val TICK_MS = 10_000L           // the status screen's presence read from the server
        const val LOOK_MS = 2_000L            // the setup screen's look at its list and the cable
        const val UNSEEN_MS = 45_000L         // a board not advertising for this long leaves the setup list
    }

    private lateinit var steps: List<TextView>
    private lateinit var stepTexts: List<String>
    private lateinit var setupStatus: StatusBlock   // the status blocks under the action rows (StatusBlock.kt)
    private lateinit var mainStatus: StatusBlock
    private lateinit var sendButton: Button
    private lateinit var setupMore: android.widget.ImageButton
    // the setup screen's rare action (its three-dots menu): Save without checking after a check that may have failed
    // only where the board is now
    private var anywayOffered = false
    private lateinit var devices: RadioGroup
    private lateinit var ssidField: EditText
    private lateinit var passwordField: EditText
    private lateinit var statusPanel: View
    private lateinit var setupPanel: View
    private lateinit var mineList: LinearLayout
    private lateinit var pendingList: TextView
    private lateinit var more: android.widget.ImageButton
    private lateinit var takePhoto: Button
    private lateinit var takeVideo: Button
    private lateinit var tuneFocus: Button                  // a flagged camera of the picked board (DESIGN.md 5.3.17)

    private val main = Handler(Looper.getMainLooper())
    private val adapter: BluetoothAdapter? by lazy {
        (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }
    private val found = LinkedHashMap<String, Pair<BluetoothDevice, String>>()   // address -> device, name
    private val seen = HashMap<String, Long>()       // address -> last advertisement (elapsedRealtime ms)
    private val ids = HashMap<String, String>()   // address -> device ID from the advertisement
    private val stored = HashMap<String, Boolean>()   // address -> the board has a Wi-Fi network (flags bit 0)
    private val usbPower = HashMap<String, Boolean>()   // address -> the board has USB power (flags bit 1)
    // device ID -> its battery (DESIGN.md 5.3.9): the K230's last reading on battery (mV) and USB power (charging), from
    // whichever channel told last: the server (check-in), the BLE advertisement, the USB cable's ID
    private val batteryMv = HashMap<String, Int>()
    private val charging = HashMap<String, Boolean>()
    private val focusCam = HashMap<String, Int>()     // device ID -> the camera to tune (DESIGN.md 5.3.17), none: absent
    private val rssi = HashMap<String, Int>()   // address -> the last advertisement's signal (dBm)
    private val rowLogged = HashMap<String, String>()   // address -> the power and bars last logged (test129)
    private val rowDrawn = HashMap<String, String>()    // address -> the power, bars and battery its row shows
    private val rowDrawnAt = HashMap<String, Long>()    // address -> when its row was last redrawn
    private val ticker = Handler(Looper.getMainLooper())   // the status screen's refresh; main's callbacks get cleared
    private val looker = Handler(Looper.getMainLooper())   // the setup screen's search
    private var onStatus = false
    private var changeFor = ""           // the Change Wi-Fi screen's board ("": the setup screen)
    private val changeOnly get() = changeFor.isNotEmpty()
    private val ssids = HashMap<String, String>()   // device ID -> the network it checked in from (server; not kept)
    /* the server is the one source of truth (DESIGN.md 5.3.5): this phone's boards are the server's list, held here
     * only in memory, never stored on the phone */
    private var mine = JSONObject()
    private val phoneId by lazy { PushService.phoneId(this) }   // the phone as the server knows its user
    private var selected = ""                        // the LEAKCAM picked on the status screen (radio)
    private var rows = listOf<Triple<String, String, String>>()   // the remembered ones: ID, name, presence line
    private val online = HashMap<String, Boolean>()   // device ID -> the server sees it online (a Wi-Fi icon)
    private val nextAt = HashMap<String, Long>()      // device ID -> its next check-in due (unix s, server), 0 unknown
    private val pend = HashMap<String, org.json.JSONArray>()   // device ID -> what waits for its check-ins (server)
    private val medias = HashMap<String, org.json.JSONArray>() // device ID -> its photos and videos (server, newest first)
    private var shown = ""                           // what the list shows now: rebuilt only when it changes
    private var scanning = false
    private var step = STEP_PLUG

    // one send at a time
    private var gatt: BluetoothGatt? = null
    private var boardName = ""
    private var boardId = ""             // "" for a board that advertises no ID: not remembered
    private var checked = true           // this send asks the board to check the network first
    private var committed = false        // commit written: STATUS reports from here on
    private var lastState = -1           // the last STATUS state shown: a read repeating it changes nothing
    private var sending = false
    private var device: BluetoothDevice? = null
    private var attempt = 0              // this send's id in COMMIT; STATUS answers for it
    private var planned = listOf<Pair<UUID, ByteArray>>()
    private var tries = 0                // connects of this send before the commit
    private var reconciling = false      // the link was lost after the commit: reconnecting to read STATUS
    private var commitAt = 0L            // elapsedRealtime of the commit: the server's check-in must be newer
    private var held = false             // a send's result on the screen: a board leaving the list does not replace it
    private var heldFor = ""            // the row (address or USB_TAG) of that send: the same board found again keeps it
    private var firstRead = false        // the first STATUS after a reconnect decides
    private var adapterReceiver: BroadcastReceiver? = null
    private var ssidText = ""
    private var writes = ArrayDeque<Pair<UUID, ByteArray>>()
    private var bondReceiver: BroadcastReceiver? = null
    private var pinReceiver: BroadcastReceiver? = null
    private var pin = ""                 // this send's pairing PIN, from the server (never stored)
    private var settingsOpened = false   // the denied-permission link opened Android's settings
    private var pinAdded = ""            // the board this send's PIN fetch made one of mine: taken back if it fails
    private var discovered = false       // services discovered on this connection
    private var bonded = false           // and encrypted: the writes can start

    // the USB cable (UsbLink.kt)
    private lateinit var usbLink: UsbLink
    private val usbTimer = Handler(Looper.getMainLooper())   // main's callbacks get cleared by every scan stop
    private var usbId = ""               // the board on the cable (its ID answer), "" when none
    private var usbStored = false        // it has a network stored
    private var usbFull = false          // the full firmware answers (ID's last field): SETUP first, then the setup's lines
    private var usbSwitching = false     // SETUP answered OK: the link goes and comes back with the setup firmware
    private var usbLater = ArrayDeque<String>()   // the setup firmware's lines, sent after the switch; never logged
    private var viaUsb = false           // this send goes over the cable
    private var usbSteps = ArrayDeque<String>()   // the lines still to send; never logged (credentials)
    private var usbAwait: String? = null // the command whose answer is awaited
    private var usbIdTries = 0           // IDs asked on this link
    private var usbOn = false            // the cable is watched: only while the setup screen is open

    private var timeScale = 1L                // 3: fast timing for the bench tests (debug builds only)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (BuildConfig.DEBUG && android.provider.Settings.Global.getInt(contentResolver, "leakcam_fast_timing", 0) == 1) {
            timeScale = 3L
            Log.i(TAG, "fast timing (bench)")
        }
        setContentView(R.layout.activity_main)
        steps = listOf(R.id.step1, R.id.step2, R.id.step3, R.id.step4).map { findViewById(it) }
        stepTexts = steps.map { it.text.toString() }
        setupStatus = StatusBlock(findViewById(R.id.setup_status))
        mainStatus = StatusBlock(findViewById(R.id.main_status))
        statusPanel = findViewById(R.id.status_panel)
        setupPanel = findViewById(R.id.setup_panel)
        mineList = findViewById(R.id.mine_list)
        pendingList = findViewById(R.id.pending_list)
        more = findViewById(R.id.more)
        takePhoto = findViewById(R.id.take_photo)
        takeVideo = findViewById(R.id.take_video)
        takePhoto.setOnClickListener { capture(selected, "photo") }
        takeVideo.setOnClickListener { capture(selected, "video") }
        tuneFocus = findViewById(R.id.tune_focus)
        tuneFocus.setOnClickListener { openFocus(selected) }
        // the rare actions, one menu next to the action buttons (user 2026-11-07: no button or menu in the rows;
        // Change Wi-Fi is rare too, photo and video are the common actions); a new board: the app menu's Setup
        more.setOnClickListener { v ->
            val id = selected
            android.widget.PopupMenu(this, v).apply {
                if (id.isEmpty())
                    return@setOnClickListener
                menu.add(R.string.setup_change).setOnMenuItemClickListener { openSetup(id); true }
                menu.add(R.string.focus_menu).setOnMenuItemClickListener { openFocus(id); true }
                menu.add(R.string.delete).setOnMenuItemClickListener { forget(id); true }
                show()
            }
        }
        sendButton = findViewById(R.id.send)
        setupMore = findViewById(R.id.setup_more)
        devices = findViewById(R.id.devices)
        ssidField = findViewById(R.id.ssid)
        passwordField = findViewById(R.id.password)

        // targetSdk 35 draws edge to edge: keep the content clear of the bars and the keyboard
        val root = findViewById<View>(R.id.root)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            root.setOnApplyWindowInsetsListener { v, insets ->
                val b = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime())
                v.setPadding(b.left, b.top, b.right, b.bottom)
                insets
            }
        }
        findViewById<CheckBox>(R.id.show_password).setOnCheckedChangeListener { _, show ->
            passwordField.inputType = InputType.TYPE_CLASS_TEXT or
                if (show) InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD else InputType.TYPE_TEXT_VARIATION_PASSWORD
            passwordField.setSelection(passwordField.text.length)
        }
        // a board leaving the list clears the pick: the last send's result stays (test110, 2026-11-09: the board finished
        // its check alone, left for the full firmware, and "Plug the LEAKCAM in" replaced the answer); a pick shows help
        devices.setOnCheckedChangeListener { _, id ->
            if (id != -1 && pickedTag() != heldFor) held = false
            if (gatt == null && !sending && !held) wifiHelp()
        }
        sendButton.setOnClickListener { if (ready()) send(true) }
        setupMore.setOnClickListener { v ->
            android.widget.PopupMenu(this, v).apply {
                if (anywayOffered)
                    menu.add(R.string.save_anyway).setOnMenuItemClickListener { if (ready()) send(false); true }
                show()
            }
        }
        // the app's menu, the usual three lines (user 2026-11-07): the screens; not while a setup is sent
        findViewById<View>(R.id.nav_menu).setOnClickListener { v ->
            android.widget.PopupMenu(this, v).apply {
                if (known().length() > 0)        // no boards yet: Setup alone (user 2026-11-07)
                    menu.add(R.string.mine_title).setOnMenuItemClickListener { if (!sending) showStatus(); true }
                menu.add(R.string.nav_setup).setOnMenuItemClickListener { if (!sending) openSetup(null); true }
                show()
            }
        }
        deleteSharedPreferences("devices")    // the list an older version kept: the server holds it now (DESIGN.md 5.3.5)
        Log.i(TAG, "phone $phoneId")      // the tests register this phone at the server by it
        PushService.register(this)        // the server can push the leak alert to this phone (DESIGN.md 5.3.13)
        // two screens (user 2026-11-07): the status of this phone's boards (the server's list, no Bluetooth or cable
        // search), and the setup screen that keeps looking for LEAKCAMs; an empty status list opens the setup at once
        // (loadMine), so does a LEAKCAM plugged in (the plug opened the app)
        usbLink = UsbLink(this, ::onUsbLine, ::onUsbUp)
        val plug = IntentFilter(UsbManager.ACTION_USB_DEVICE_ATTACHED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            registerReceiver(plugReceiver, plug, Context.RECEIVER_EXPORTED)   // attach comes from the system
        else
            registerReceiver(plugReceiver, plug)
        when {
            intent.hasExtra(LeakAlert.EXTRA_ID) -> showLeak(intent)   // started by a leak alert: its leak panel
            intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED -> openSetup(null)
            else -> showStatus()
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        when {
            intent.hasExtra(LeakAlert.EXTRA_ID) -> showLeak(intent)
            intent.action != UsbManager.ACTION_USB_DEVICE_ATTACHED -> {}
            onStatus -> openSetup(null)      // opened again by a LEAKCAM plugged in (singleTop): set it up
            else -> usbLink.find()
        }
    }

    /** opened from a leak alert ("I see it"): that board picked on the status screen with its leak panel on top:
     *  which board, when, what found it, and its record (4 pictures, a 15 s video) as the board uploads it */
    private fun showLeak(i: Intent) {
        val id = i.getStringExtra(LeakAlert.EXTRA_ID) ?: return
        selected = id
        showStatus()
        findViewById<View>(R.id.leak_panel).visibility = View.VISIBLE
        findViewById<TextView>(R.id.leak_title).text = i.getStringExtra(LeakAlert.EXTRA_TITLE) ?: getString(R.string.alert_title, id)
        findViewById<TextView>(R.id.leak_text).text = i.getCharSequenceExtra(LeakAlert.EXTRA_TEXT) ?: ""
        findViewById<View>(R.id.leak_not).setOnClickListener { notLeak(id) }
        // one tile per picture and video ID the push announced (pictures in a row, the video under them), a spinner each
        // until the gallery has downloaded it by its ID (DESIGN.md 5.3.13)
        val ids = i.getStringExtra(LeakAlert.EXTRA_MEDIA).orEmpty().split(",").filter { it.isNotBlank() }
        // one preview: the first file downloaded (a spinner until then), "+N more" for the others; a tap on the preview
        // opens that file alone, on "+N more" the phone's gallery with them all (user 2026-11-07)
        val preview = findViewById<android.widget.FrameLayout>(R.id.leak_preview)
        val more = findViewById<TextView>(R.id.leak_more)
        preview.visibility = if (ids.isNotEmpty()) View.VISIBLE else View.GONE
        more.visibility = if (ids.size > 1) View.VISIBLE else View.GONE
        more.text = getString(R.string.leak_more, ids.size - 1)
        more.setOnClickListener { gallery.open() }
        var shown = false
        for (mid in ids)
            gallery.fetch(mid, preview) { uri, thumb ->
                if (!shown) {
                    shown = true
                    val video = mid.substringAfterLast('-').startsWith("v")
                    gallery.show(preview, video, thumb)
                    preview.setOnClickListener { gallery.view(uri, video) }
                }
            }
        Log.i(TAG, "leak panel $id")
    }

    /** Not a leak (user 2026-11-07): the server clears it and re-arms the check (a wet probe once it has dried, the
     *  camera inference on a new reference picture); the panel goes */
    private fun notLeak(id: String) {
        mainStatus.busy(getString(R.string.busy_sending))
        Thread {
            val ok = try {
                val c = URL("${BuildConfig.SERVER}/v1/devices/$id/clear?not_leak=1").openConnection() as HttpURLConnection
                c.requestMethod = "POST"
                c.connectTimeout = 5000
                c.readTimeout = 5000
                (c.responseCode == 200).also { c.disconnect() }
            } catch (e: Exception) {
                false
            }
            runOnUiThread {
                Log.i(TAG, "not a leak $id: ${if (ok) "cleared" else "failed"}")
                mainStatus.busy(null)
                if (!ok)
                    return@runOnUiThread android.widget.Toast.makeText(this, R.string.err_not_leak,
                        android.widget.Toast.LENGTH_LONG).show()
                findViewById<View>(R.id.leak_panel).visibility = View.GONE
                note = Triple(id, getString(R.string.not_leak_done, id, checkinAt(nextAt[id] ?: 0)), SystemClock.elapsedRealtime() + 60_000L)
                shown = ""
                render()
            }
        }.start()
    }

    override fun onDestroy() {
        unregisterReceiver(plugReceiver)
        stopUsb()
        ticker.removeCallbacksAndMessages(null)
        stopScan()
        finishSend(null)
        super.onDestroy()
    }

    // ---------------- the steps and the help box ----------------

    /** marks the steps before [current] done and [current] active, and says what to do now */
    private fun showText(current: Int, text: String, error: Boolean) {
        step = current
        steps.forEachIndexed { i, v ->
            val n = i + 1
            v.text = if (n < current) "✓ ${stepTexts[i]}" else stepTexts[i]
            v.setTextColor(getColor(when {
                n < current -> R.color.step_done
                n == current -> R.color.step_current
                else -> R.color.step_todo
            }))
            v.setTypeface(null, if (n == current) Typeface.BOLD else Typeface.NORMAL)
        }
        setupStatus.text.text = text
        // each screen change in the log: setup timings (tests/test106 timeline.py) and support; no passphrase in any text
        Log.i(TAG, "step $current${if (error) " error" else ""}: ${text.lineSequence().first()}")
        setupStatus.error(error)
    }

    private fun show(current: Int, res: Int, vararg args: Any) = showText(current, getString(res, *args), false)

    private fun problem(current: Int, res: Int, vararg args: Any) = showText(current, getString(res, *args), true)

    /** a denied permission: the message and a link to the app's page in Android's settings (user 2026-11-08: "app needs
     *  to help me to find setting again, with some link in status text, not only message"); after "don't allow" Android
     *  may not ask again, so the settings page is the way back. onResume goes on once it is allowed there */
    private fun deniedWithLink(current: Int, res: Int) {
        problem(current, res)
        val link = getString(R.string.open_settings)
        val text = android.text.SpannableStringBuilder(setupStatus.text.text).append("\n\n").append(link)
        text.setSpan(object : android.text.style.ClickableSpan() {
            override fun onClick(v: View) {
                settingsOpened = true
                Log.i(TAG, "settings opened for a permission")
                startActivity(Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                                     android.net.Uri.parse("package:$packageName")))
            }
        }, text.length - link.length, text.length, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        setupStatus.text.text = text
        setupStatus.text.movementMethod = android.text.method.LinkMovementMethod.getInstance()
    }

    // ---------------- permissions and Bluetooth on ----------------

    private fun neededPermissions(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)

    private fun permissionsGranted() =
        neededPermissions().all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    /** true when scanning and connecting can start now; otherwise asks for what is missing */
    private fun ready(): Boolean {
        val a = adapter
        if (a == null) {
            problem(step, R.string.err_no_bluetooth)
            return false
        }
        if (!permissionsGranted()) {
            requestPermissions(neededPermissions(), REQUEST_PERMISSIONS)
            return false
        }
        if (!a.isEnabled) {
            problem(step, R.string.err_bluetooth_off)
            @Suppress("DEPRECATION")
            startActivityForResult(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQUEST_ENABLE_BT)
            return false
        }
        return true
    }

    // ---------------- the leak alert's permissions (DESIGN.md 5.3.13), asked once a board is set up ----------------

    private var askingAlert = false

    /** the first missing one asked for: notifications, then full screen (Android 14+), then Do Not Disturb access;
     *  again from onResume after each settings screen until all are there */
    private val gallery by lazy { Gallery(this) }

    private var note: Triple<String, String, Long>? = null   // the last action's answer for a board, until its time

    /** LEAKCAM Focus Adjustment for the board (DESIGN.md 5.3.17): the flagged camera first */
    private fun openFocus(id: String) {
        if (id.isNotEmpty())
            startActivity(Intent(this, FocusActivity::class.java)
                .putExtra(FocusActivity.EXTRA_ID, id).putExtra(FocusActivity.EXTRA_CAM, focusCam[id] ?: 0))
    }

    /** Take photo / Take video (DESIGN.md 5.3.14): the request to the server, handed over at the board's next check-in;
     *  the app is done at once and says when it happens (the 15 s rule); a push comes when it is ready to play */
    private fun capture(id: String, kind: String) {
        val what = getString(if (kind == "video") R.string.kind_video else R.string.kind_photo)
        mainStatus.busy(getString(R.string.busy_sending))
        Thread {
            val (within, at) = try {
                val c = URL("${BuildConfig.SERVER}/v1/devices/$id/capture").openConnection() as HttpURLConnection
                c.requestMethod = "POST"
                c.doOutput = true
                c.connectTimeout = 5000
                c.readTimeout = 5000
                c.setRequestProperty("Content-Type", "application/json")
                c.outputStream.use { it.write(JSONObject().put("kind", kind).toString().toByteArray()) }
                val j = if (c.responseCode == 200) JSONObject(c.inputStream.bufferedReader().readText()) else null
                c.disconnect()
                (j?.optInt("within_s", -1) ?: -1) to (j?.optLong("at", 0) ?: 0L)
            } catch (e: Exception) {
                -1 to 0L
            }
            Log.i(TAG, "capture $kind $id within $within")   // test: the request reached the server
            runOnUiThread {
                // kept over the status screen's refreshes for a minute (the presence redraw replaced it at once)
                note = Triple(id, if (within < 0) getString(R.string.capture_failed)
                                  else getString(R.string.capture_later, id, what, checkinAt(at, within)),
                              SystemClock.elapsedRealtime() + 60_000L)
                mainStatus.busy(null)
                shown = ""
                render()                // the note, then the presence with its next check-in
                loadMine()              // the pending list with it at once
            }
        }.start()
    }

    /** when a board's next check-in comes, in the phone's time zone and clock format (user 2026-11-07): "about
     *  21:52" (a date when not today), "any moment now" when due; else the interval, or unknown */
    private fun checkinAt(at: Long, within: Int = -1): String {
        val ms = at * 1000
        return when {
            at <= 0 && within >= 0 ->
                getString(R.string.checkin_within, if (within >= 120) "${within / 60} min" else "$within s")
            at <= 0 -> getString(R.string.checkin_unknown)
            ms - System.currentTimeMillis() < 30_000 -> getString(R.string.checkin_now)
            else -> getString(R.string.checkin_at, android.text.format.DateUtils.formatDateTime(this, ms,
                android.text.format.DateUtils.FORMAT_SHOW_TIME or if (android.text.format.DateUtils.isToday(ms)) 0
                else android.text.format.DateUtils.FORMAT_SHOW_DATE or android.text.format.DateUtils.FORMAT_ABBREV_MONTH))
        }
    }

    /** the setup screen's three dots: there (keeping its place when empty) only when it has an item */
    private fun setupMenu() {
        setupMore.visibility = if (anywayOffered) View.VISIBLE else View.INVISIBLE
    }

    private fun alertPermissions() {
        LeakAlert.channel(this)
        askingAlert = true
        when {
            Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED ->
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQUEST_NOTIFY)
            !LeakAlert.fullScreenAllowed(this) ->
                startActivity(Intent(android.provider.Settings.ACTION_MANAGE_APP_USE_FULL_SCREEN_INTENT,
                                     android.net.Uri.parse("package:$packageName")))
            !LeakAlert.dndAccess(this) ->
                startActivity(Intent(android.provider.Settings.ACTION_NOTIFICATION_POLICY_ACCESS_SETTINGS))
            else -> {
                askingAlert = false
                Log.i(TAG, "alert permissions all granted")
            }
        }
    }

    override fun onResume() {
        super.onResume()
        if (settingsOpened) {                // back from the settings link: go on when it was allowed there
            settingsOpened = false
            if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED &&
                !LeakAlert.fullScreenAllowed(this))
                askingAlert = true
            if (!onStatus && permissionsGranted()) {
                setupStatus.text.movementMethod = null
                wifiHelp()
                if (ready())
                    keepLooking()
            }
        }
        if (askingAlert)
            alertPermissions()
        if (onStatus)
            render()                         // back from the notification settings: the line goes once allowed
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        if (requestCode == REQUEST_NOTIFY) {
            // denied: not asked again at once (a "don't allow" answers at once: it looped); the link instead
            if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                askingAlert = false
                return deniedWithLink(step, R.string.err_notify_permission)
            }
            alertPermissions()
            return
        }
        if (requestCode != REQUEST_PERMISSIONS)
            return
        if (!permissionsGranted())
            return deniedWithLink(step, R.string.err_permission)
        wifiHelp()
        if (ready())
            keepLooking()            // no tap again: the setup screen looks by itself
    }

    @Deprecated("the Bluetooth switch answers here")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_ENABLE_BT && adapter?.isEnabled == true) {
            wifiHelp()
            keepLooking()
        }
    }

    // ---------------- step 2: find ----------------

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            main.post { addDevice(result) }
        }

        override fun onScanFailed(errorCode: Int) {
            main.post {
                stopScan()
                problem(STEP_FIND, R.string.err_scan, errorCode)
            }
        }
    }

    /** the setup screen's search (user 2026-11-07: it keeps looking for BLE and USB): one Bluetooth scan for as
     *  long as the screen is open (Android drops an app that starts more than 5 in 30 s), and look() */
    private fun startScan() {
        val scanner = adapter?.bluetoothLeScanner ?: return problem(STEP_FIND, R.string.err_bluetooth_off)
        stopScan()
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        Log.i(TAG, "scan on")       // test135: never on the status screen
        scanning = true
        updateBusy()
        look(0)
    }

    /** every 2 s while the setup screen looks: a board not heard for 45 s leaves the list (a board on battery
     *  advertises in windows: not at every gap), the cable asked again, and after 10 s with nothing found how to
     *  bring one */
    private fun look(n: Int) {
        if (onStatus || sending)
            return
        val now = SystemClock.elapsedRealtime()
        for (a in found.keys.filter { now - (seen[it] ?: 0L) > UNSEEN_MS / timeScale }) {
            found.remove(a)
            devices.findViewWithTag<View>(a)?.let {
                if (it.id == devices.checkedRadioButtonId)
                    devices.clearCheck()        // the picked board gone: looking again, the spinner back
                devices.removeView(it)
            }
            updateBusy()
        }
        if (!usbLink.up)
            usbLink.find()
        if (n == (SCAN_MS / LOOK_MS).toInt() && devices.childCount == 0 && step != STEP_DONE)
            show(STEP_PLUG,                 // help, not an error: it goes on looking
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) R.string.help_none_found_location
                else R.string.help_none_found)
        looker.postDelayed({ look(n + 1) }, LOOK_MS / timeScale)
    }

    private fun stopScan() {
        looker.removeCallbacksAndMessages(null)
        if (scanning && permissionsGranted()) {
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
            Log.i(TAG, "scan off")
        }
        scanning = false
        updateBusy()
    }

    /* the signal as 1-4 bars, never the dB value on the screen; the value on a long press (user 2026-11-04) */

    private fun bars(dbm: Int) = when (barCount(dbm)) {
        4 -> R.drawable.ic_signal_4
        3 -> R.drawable.ic_signal_3
        2 -> R.drawable.ic_signal_2
        else -> R.drawable.ic_signal_1
    }

    private fun showDbm(address: String) {
        val dbm = rssi[address] ?: return
        Log.i(TAG, "dbm $dbm")   // test118: the long press
        android.widget.Toast.makeText(this, getString(R.string.signal_dbm, dbm), android.widget.Toast.LENGTH_SHORT).show()
    }

    /** one icon of a board's row: its picture, its description (screen readers, tests), its tooltip */
    private class RowIcon(val drawable: android.graphics.drawable.Drawable, val what: String, val tip: String? = null)

    /** a board's status icons, the same in every list (user 2026-11-07: "always same look and feel for radios"):
     *  Wi-Fi = present at the server (one of mine); the cable, or Bluetooth + its signal bars when in range; the
     *  battery filled to its charge, green on USB power, once anything about it is known (DESIGN.md 5.3.4, 5.3.9) */
    private fun rowIcons(id: String, address: String?): List<RowIcon> {
        val icons = mutableListOf<RowIcon>()
        if (known().has(id) && online[id] == true)
            icons.add(RowIcon(getDrawable(R.drawable.ic_wifi)!!, getString(R.string.icon_wifi)))
        if (address == USB_TAG)
            icons.add(RowIcon(getDrawable(R.drawable.ic_usb)!!, getString(R.string.icon_usb)))   // the link implies its power
        else if (address != null) {
            icons.add(RowIcon(getDrawable(R.drawable.ic_bluetooth)!!, getString(R.string.icon_bluetooth)))
            icons.add(RowIcon(getDrawable(bars(rssi[address] ?: -100))!!, getString(R.string.icon_signal)))
        }
        focusCam[id]?.let {                                  // a camera to tune: until it is (5.3.17)
            icons.add(RowIcon(getDrawable(R.drawable.ic_alert)!!, getString(R.string.icon_focus, it + 1),
                              getString(R.string.icon_focus, it + 1)))
        }
        val chg = onUsbPower(id, address)
        val pct = batteryMv[id]?.let { BatteryIcon.percent(it) }
        // the battery once anything about it is known (in range, a level, or the server's `charging` alone: the
        // outline, test131 step 6)
        if (pct != null || address != null || charging.containsKey(id))
            // on USB power the K230 reads the charger's rail: the level is the last one on battery
            icons.add(RowIcon(BatteryIcon(pct, chg, resources.displayMetrics.density),
                              getString(if (chg) R.string.icon_plug else R.string.icon_battery),
                              when {
                                  pct == null && chg -> getString(R.string.battery_tip_charging_unknown)
                                  pct == null -> getString(R.string.battery_tip_unknown)
                                  chg -> getString(R.string.battery_tip_charging, pct)
                                  else -> getString(R.string.battery_tip, pct)
                              }))
        return icons
    }

    /** on USB power: the cable link, the advertisement's flag in range, else the server's `charging` */
    private fun onUsbPower(id: String, address: String?) = address == USB_TAG ||
        (address != null && usbPower[address] == true) || (address == null && charging[id] == true)

    /** the row's icons as one drawable after a radio's text (the setup list's RadioGroup takes only RadioButtons):
     *  the same icons and the same gaps as the status list's row */
    private fun iconStrip(id: String, address: String?): android.graphics.drawable.Drawable? {
        val icons = rowIcons(id, address).map { it.drawable }
        if (icons.isEmpty())
            return null
        // drawn side by side onto one bitmap: a layered drawable overlapped the battery (it has no width of its own)
        val px = (24 * resources.displayMetrics.density).toInt()
        val gap = (6 * resources.displayMetrics.density).toInt()
        val bmp = android.graphics.Bitmap.createBitmap(icons.size * (gap + px), px, android.graphics.Bitmap.Config.ARGB_8888)
        val canvas = android.graphics.Canvas(bmp)
        icons.forEachIndexed { n, d ->
            val x = gap + n * (gap + px)
            d.setBounds(x, 0, x + px, px)
            d.draw(canvas)
        }
        return android.graphics.drawable.BitmapDrawable(resources, bmp).apply { setBounds(0, 0, bmp.width, bmp.height) }
    }

    /** a status list radio: "serial · SSID" (the setup list: the serial only) */
    private fun deviceText(id: String) = listOfNotNull(id, ssids[id]?.takeIf { it.isNotEmpty() }).joinToString(" · ")

    private fun addDevice(r: ScanResult) {
        val d = r.device
        seen[d.address] = SystemClock.elapsedRealtime()
        val mfg = r.scanRecord?.getManufacturerSpecificData(0xFFFF)
        // every advertisement: the signal and the board's power change while it is listed (DESIGN.md 5.3.4); the
        // status screen shows them at its next refresh (every 10 s): a redraw per advertisement lost test111's taps
        rssi[d.address] = r.rssi
        usbPower[d.address] = mfg != null && mfg.size >= 7 && (mfg[6].toInt() and 2) != 0
        val devId = mfg?.takeIf { it.size >= 6 }?.copyOfRange(0, 6)?.joinToString("") { "%02X".format(it) }
        if (devId != null && mfg != null && mfg.size >= 8) {   // the battery in 20 mV steps, 0xFF none
            val b = mfg[7].toInt() and 0xFF
            if (b != 0xFF)
                batteryMv[devId] = b * 20
            charging[devId] = usbPower[d.address] == true
        }
        if (devId != null && !known().has(devId)) {
            // the setup row's power and signal, logged when they change (test129)
            val power = if (usbPower[d.address] == true) "plug" else "battery"
            val key = "$power ${barCount(r.rssi)}"
            if (rowLogged[d.address] != key) {
                rowLogged[d.address] = key
                Log.i(TAG, "setup row $devId power $power bars ${barCount(r.rssi)} rssi ${r.rssi}")
            }
        }
        if (found.containsKey(d.address)) {
            // the same row refreshed (its bars, power, battery) without a redraw of the list, and only when one of its
            // icons changed: redrawn at every advertisement (5 a second) the screen was never idle and each UI
            // Automator lookup waited 10 s for it (found after 17 s instead of 0.2 s, test106 2026-11-08)
            // and at most once a second: the bars flip 3 <-> 4 several times a second on the bench
            val drawn = "${usbPower[d.address]} ${barCount(r.rssi)} ${devId?.let { batteryMv[it] }}"
            val now = SystemClock.elapsedRealtime()
            if (rowDrawn[d.address] != drawn && now - (rowDrawnAt[d.address] ?: 0L) >= 1000) {
                rowDrawn[d.address] = drawn
                rowDrawnAt[d.address] = now
                devices.findViewWithTag<RadioButton>(d.address)?.let { rb ->
                    rb.setCompoundDrawablesRelative(null, null, iconStrip(ids[d.address] ?: "", d.address), null)
                }
            }
            return
        }
        // a LEAKCAM is named by its serial everywhere (the device ID: USB serial number, BLE name, server; user
        // 2026-11-01: no "LEAKCAM-" prefix)
        val name = devId ?: r.scanRecord?.deviceName ?: d.name ?: d.address
        if (devId != null)
            ids[d.address] = devId
        if (devId != null && devId == usbId)
            return                          // on the cable: set up over USB, not listed twice
        // only new devices: a board set up from this phone is never listed (user 2026-11-07, DESIGN.md 5.3.4)
        if (devId != null && known().has(devId))
            return
        // one setup at a time, the nearest board (user 2026-11-08: "take strongest signal"): one row only; a new
        // board clearly stronger (SWAP_DB, against flapping) than the shown one takes its place before a send; the
        // cable's board always stays
        val shown = devices.findViewById<View>(devices.checkedRadioButtonId)
        if (shown != null) {
            val shownAt = shown.tag as? String
            if (sending || shownAt == USB_TAG || r.rssi < (rssi[shownAt] ?: -127) + SWAP_DB)
                return
            Log.i(TAG, "setup row swapped for a stronger board: ${r.rssi} dBm over ${rssi[shownAt]} dBm")
            found.remove(shownAt)
            devices.clearCheck()
            devices.removeView(shown)
        }
        stored[d.address] = mfg != null && mfg.size >= 7 && (mfg[6].toInt() and 1) != 0
        Log.i(TAG, "found $name id ${devId ?: "none"} creds ${if (stored[d.address] == true) 1 else 0}")   // test111
        found[d.address] = d to name
        devices.addView(RadioButton(this).apply {
            id = View.generateViewId()
            tag = d.address
            text = name               // the serial only: the setup list is for a new device (user 2026-11-07)
            setCompoundDrawablesRelative(null, null, iconStrip(name, d.address), null)
            setOnLongClickListener { showDbm(d.address); true }
        })
        // the first board is picked at once, the search goes on behind (another board nearby joins the list): waiting
        // for the whole Find took 10 s of the setup, then 1.7 s (test106); the cable's board takes the pick (USB > BLE)
        if (devices.checkedRadioButtonId == -1 && !sending)
            devices.check(devices.findViewWithTag<View>(d.address).id)
        updateBusy()
    }

    /** what to do now for the picked board; the hints say when the board has a network the app cannot show */
    private fun wifiHelp() {
        val tag = pickedTag()
        // the setup screen is for a new device: blank fields; Change Wi-Fi shows the network it is on (not secret) and
        // its password as dots (user 2026-11-07)
        ssidField.hint = getString(R.string.ssid)
        passwordField.hint = getString(if (changeOnly && ssids[changeFor] != null) R.string.hidden_password else R.string.password)
        when {
            changeOnly -> show(STEP_WIFI, R.string.help_change, changeFor, checkinAt(nextAt[changeFor] ?: 0))
            tag == USB_TAG -> show(STEP_WIFI, R.string.help_usb_wifi, usbId)
            tag == null -> show(STEP_FIND, R.string.help_plug)
            else -> show(STEP_WIFI, R.string.help_wifi)
        }
    }

    private fun pickedTag() = devices.findViewById<RadioButton>(devices.checkedRadioButtonId)?.tag as String?

    /** the setup screen's spinner line (StatusBlock): sending, else looking, else none */
    /** "Looking..." only until a board is found: one setup at a time (user 2026-11-08: "once LEAKCAM is found, stop
     *  spinner"); the scan goes on quietly for the found row's live icons */
    private fun updateBusy() = setupStatus.busy(when {
        sending -> getString(R.string.busy_sending)
        scanning && devices.checkedRadioButtonId == -1 -> getString(R.string.looking)
        else -> null
    })

    /** the setup screen goes on looking after a send that failed */
    private fun keepLooking() {
        if (!onStatus && !scanning && permissionsGranted() && adapter?.isEnabled == true)
            startScan()
    }

    // ---------------- the status screen ----------------

    /** the LEAKCAMs set up from this phone and their presence (the server's list, refreshed every 10 s); no Bluetooth
     *  or cable search here (user 2026-11-07): a LEAKCAM plugged in opens the setup screen (plugReceiver) */
    private fun showStatus() {
        onStatus = true
        Log.i(TAG, "screen status")      // test135
        stopScan()
        stopUsb()
        setupPanel.visibility = View.GONE
        statusPanel.visibility = View.VISIBLE
        findViewById<TextView>(R.id.screen_title).setText(R.string.mine_title)
        shown = ""                      // back on the list: drawn again
        render()
        ticker.removeCallbacksAndMessages(null)
        tick()
    }

    private fun tick() {
        if (!onStatus)
            return
        refreshPresence()
        ticker.postDelayed({ tick() }, TICK_MS / timeScale)
    }

    /** the status list: this phone's LEAKCAMs as the server sees them, one radio each (presence, network, battery
     *  icons); the action row acts on the picked one: Take photo / Take video, the rest in the three dots */
    private fun render() {
        val k = known()
        val all = rows.filter { k.has(it.first) }
        if (all.none { it.first == selected })
            selected = all.firstOrNull()?.first.orEmpty()
        // every presence refresh lands here: the views are rebuilt only for a change, a rebuild under a finger lost
        // the tap (test111's Delete, 2026-11-01)
        val now = all.joinToString("|") {
            "${it.first},${it.third},${online[it.first]},${batteryMv[it.first]},${charging[it.first]},${ssids[it.first]},${pend[it.first]},${medias[it.first]}"
        } + "|$selected"
        if (now == shown)
            return
        shown = now
        mineList.removeAllViews()
        for ((id, _, _) in all)
            mineList.addView(LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = android.view.Gravity.CENTER_VERTICAL    // the name and the icons on one line
                val up = online[id] == true
                addView(RadioButton(context).apply {
                    // its serial (the device ID, as on the USB serial number and the server) and its network (user:
                    // minimum text, no shared "LEAKCAM-" prefix); the state is in the icons, no presence in red
                    text = deviceText(id)
                    if (!up)
                        setTextColor(0xFFC62828.toInt())
                    isChecked = id == selected
                    contentDescription = "pick $id"
                    setOnClickListener { selected = id; render() }
                }, LinearLayout.LayoutParams(-2, -2))     // the icons right after the name, not at the far end
                val pct = batteryMv[id]?.let { BatteryIcon.percent(it) }
                Log.i(TAG, "row $id wifi ${if (up) 1 else 0} red ${if (up) 0 else 1}" +
                      " bat ${pct ?: -1} chg ${if (onUsbPower(id, null)) 1 else 0}")   // test111, test131
                val gap = (6 * resources.displayMetrics.density).toInt()   // the icons a little apart from the name
                for (icon in rowIcons(id, null))
                    addView(android.widget.ImageView(context).apply {
                        setPaddingRelative(gap, 0, 0, 0)
                        setImageDrawable(icon.drawable)
                        contentDescription = icon.what
                        tooltipText = icon.tip
                    })
            })
        // the last action's answer for a minute, then always the picked board's presence with its next check-in
        // (user 2026-11-07: "always include next checkin time in status")
        val status = android.text.SpannableStringBuilder(listOfNotNull(
            note?.takeIf { it.first == selected && SystemClock.elapsedRealtime() < it.third }?.second,
            all.firstOrNull { it.first == selected }?.third).joinToString("\n"))
        // notifications off: always said here, with the way to turn them on (user 2026-11-08: "always show we need to
        // enable notifications permission, this is always on status screen"): without them a leak reaches no one
        if (!getSystemService(android.app.NotificationManager::class.java).areNotificationsEnabled()) {
            val link = getString(R.string.notify_turn_on)
            status.append("\n\n").append(getString(R.string.notify_off)).append(" ").append(link)
            status.setSpan(object : android.text.style.ClickableSpan() {
                override fun onClick(v: View) {
                    Log.i(TAG, "notification settings opened")
                    startActivity(Intent(android.provider.Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                        .putExtra(android.provider.Settings.EXTRA_APP_PACKAGE, packageName))
                }
            }, status.length - link.length, status.length, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            mainStatus.error(true)
        } else
            mainStatus.error(false)
        mainStatus.text.text = status
        mainStatus.text.movementMethod = android.text.method.LinkMovementMethod.getInstance()
        // what waits for its check-ins, in the server's order (user 2026-11-07): one photo, one video, one Wi-Fi
        // change at most, so a button whose ask waits is off
        val waits = pend[selected] ?: org.json.JSONArray()
        val kinds = (0 until waits.length()).map { waits.getJSONObject(it).getString("kind") }
        takePhoto.isEnabled = selected.isNotEmpty() && "photo" !in kinds
        takeVideo.isEnabled = selected.isNotEmpty() && "video" !in kinds
        val toTune = focusCam[selected]                                   // offered while a camera is flagged
        tuneFocus.visibility = if (selected.isNotEmpty() && toTune != null) View.VISIBLE else View.GONE
        if (toTune != null)
            tuneFocus.text = getString(R.string.focus_offer, toTune + 1)
        pendingList.visibility = if (kinds.isEmpty()) View.GONE else View.VISIBLE
        pendingList.text = (listOf(getString(R.string.pending_title, checkinAt(nextAt[selected] ?: 0))) +
            (0 until waits.length()).map { n ->
                val w = waits.getJSONObject(n)
                val what = when (w.getString("kind")) {
                    "wifi" -> getString(if (w.optString("state") == "sent") R.string.pending_wifi_sent else R.string.pending_wifi,
                                        w.optString("ssid"))
                    "photo" -> getString(R.string.pending_photo)
                    "video" -> getString(R.string.pending_video)
                    "focus" -> getString(R.string.pending_focus)
                    else -> getString(R.string.pending_leak)
                }
                getString(R.string.pending_item, n + 1, what, android.text.format.DateUtils.formatDateTime(this,
                    w.optLong("asked") * 1000, android.text.format.DateUtils.FORMAT_SHOW_TIME))
            }).joinToString("\n")
        showMedia(medias[selected] ?: org.json.JSONArray())
    }

    /** the picked board's photos and videos as links (user 2026-11-07): a tap downloads it into the gallery (once) and
     *  opens it alone in the phone's viewer or player */
    private fun showMedia(list: org.json.JSONArray) {
        val box = findViewById<LinearLayout>(R.id.media_list)
        box.removeAllViews()
        box.visibility = if (list.length() == 0) View.GONE else View.VISIBLE
        if (list.length() == 0)
            return
        box.addView(TextView(this).apply {
            setText(R.string.media_title)
            setTypeface(null, Typeface.BOLD)
        })
        for (n in 0 until list.length()) {
            val m = list.getJSONObject(n)
            val video = m.optString("kind") == "video"
            val at = android.text.format.DateUtils.formatDateTime(this, m.optLong("at") * 1000,
                android.text.format.DateUtils.FORMAT_SHOW_TIME or android.text.format.DateUtils.FORMAT_SHOW_DATE or
                android.text.format.DateUtils.FORMAT_ABBREV_MONTH)
            box.addView(TextView(this).apply {
                text = android.text.SpannableString(getString(if (video) R.string.media_video else R.string.media_photo, at))
                    .apply { setSpan(android.text.style.UnderlineSpan(), 0, length, 0) }
                setTextColor(getColor(android.R.color.holo_blue_dark))
                setPadding(0, (6 * resources.displayMetrics.density).toInt(), 0, 0)
                setOnClickListener { openMedia(m.getString("id"), video) }
            })
        }
    }

    private fun openMedia(id: String, video: Boolean) {
        mainStatus.busy(getString(R.string.busy_opening))
        Thread {
            val uri = gallery.save(id)
            runOnUiThread {
                mainStatus.busy(null)
                if (uri == null)
                    return@runOnUiThread android.widget.Toast.makeText(this, R.string.err_media, android.widget.Toast.LENGTH_LONG).show()
                gallery.view(uri, video)
            }
        }.start()
    }

    /** the setup screen (user 2026-11-07): it keeps looking over Bluetooth and on the cable at once, a spinner under
     *  the list (id null); or the Change Wi-Fi screen of one of mine (id): only the network, sent through the server
     *  for its next check-in (DESIGN.md 5.3.13), no search (user 2026-11-07) */
    private fun openSetup(id: String?) {
        onStatus = false
        ticker.removeCallbacksAndMessages(null)
        statusPanel.visibility = View.GONE
        setupPanel.visibility = View.VISIBLE
        anywayOffered = false
        setupMenu()
        changeFor = id.orEmpty()
        Log.i(TAG, if (changeOnly) "screen change $changeFor" else "screen setup")   // test135
        findViewById<TextView>(R.id.screen_title).setText(if (changeOnly) R.string.change_title else R.string.setup_title)
        for (v in listOf<View>(findViewById(R.id.intro), steps[0], steps[1], steps[2], steps[3], devices))
            v.visibility = if (changeOnly) View.GONE else View.VISIBLE
        sendButton.setText(if (changeOnly) R.string.send_change else R.string.send)
        found.clear()                      // listed again by this screen's own search
        devices.removeAllViews()
        ssidField.setText(ssids[changeFor].orEmpty())   // the network it is on now (not secret); the passphrase hidden
        passwordField.text.clear()
        sendButton.isEnabled = true
        wifiHelp()
        if (changeOnly) {
            stopScan()
            stopUsb()
            return
        }
        startUsb()
        if (usbId.isNotEmpty())
            addUsbRadio()
        // no Find button: the list fills by itself (user 2026-11-07); ready() asks for what is missing first. A
        // running scan goes on: Android ignores an app that starts more than 5 scans in 30 s (test111)
        if (!scanning && ready())
            startScan()
    }

    /** the cable watched while the setup screen is open; the status screen only hears a plug (plugReceiver) */
    private fun startUsb() {
        if (usbOn)
            return
        usbOn = true
        usbLink.start()
        Log.i(TAG, "usb watch on")   // test135: only on the setup screen
    }

    private fun stopUsb() {
        usbTimer.removeCallbacksAndMessages(null)
        if (!usbOn)
            return
        usbOn = false
        usbLink.stop()
        Log.i(TAG, "usb watch off")
    }

    /** a LEAKCAM plugged in while the status screen is open: straight to the setup screen (user 2026-11-07) */
    private val plugReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            @Suppress("DEPRECATION")
            val d = i.getParcelableExtra<android.hardware.usb.UsbDevice>(UsbManager.EXTRA_DEVICE)
            if (onStatus && d != null && d.vendorId == UsbLink.VID && d.productId == UsbLink.PID) {
                Log.i(TAG, "usb plug: setup")   // test113
                openSetup(null)
            }
        }
    }

    /** the Change Wi-Fi screen's send: to the server (PUT /v1/devices/<id>/wifi), handed over at its next
     *  check-in; the board tests it before it keeps it, a push if it fails (DESIGN.md 5.3.13). Done at once */
    private fun sendViaServer(id: String, ssid: String, psk: String) {
        sendButton.isEnabled = false
        setupStatus.busy(getString(R.string.busy_sending))
        show(STEP_SAVE, R.string.help_server_sending, id)
        Thread {
            val (code, at) = try {
                val c = URL("${BuildConfig.SERVER}/v1/devices/$id/wifi").openConnection() as HttpURLConnection
                c.requestMethod = "PUT"
                c.doOutput = true
                c.connectTimeout = 5000
                c.readTimeout = 5000
                c.setRequestProperty("Content-Type", "application/json")
                c.outputStream.use { it.write(JSONObject().put("ssid", ssid).put("psk", psk).toString().toByteArray()) }
                val code = c.responseCode
                val at = if (code == 200) JSONObject(c.inputStream.bufferedReader().readText()).optLong("at", 0) else 0L
                c.disconnect()
                code to at
            } catch (e: Exception) {
                -1 to 0L
            }
            runOnUiThread {
                Log.i(TAG, "wifi via server $id answer $code")   // test134
                sendButton.isEnabled = true
                setupStatus.busy(null)
                if (code != 200)
                    return@runOnUiThread problem(STEP_SAVE, R.string.err_wifi_server, id)
                note = Triple(id, getString(R.string.wifi_pending, id, ssid, checkinAt(at)), SystemClock.elapsedRealtime() + 60_000L)
                showStatus()
            }
        }.start()
    }

    // ---------------- steps 3 and 4: Wi-Fi, pair and save ----------------

    private fun send(check: Boolean) {
        val address = pickedTag()
        val ssid = ssidField.text.toString().toByteArray(Charsets.UTF_8)
        val psk = passwordField.text.toString().toByteArray(Charsets.UTF_8)
        if (ssid.isEmpty() || ssid.size > 32)
            return problem(STEP_WIFI, R.string.err_ssid)
        if (psk.isNotEmpty() && psk.size !in 8..63)
            return problem(STEP_WIFI, R.string.err_password)
        // the dots stand for the stored password, which the app cannot send back: the same network needs it typed
        if (changeOnly && psk.isEmpty() && ssids[changeFor] == ssidField.text.toString())
            return problem(STEP_WIFI, R.string.err_password_retype)
        if (changeOnly)                      // one of mine: at its next check-in
            return sendViaServer(changeFor, ssidField.text.toString(), passwordField.text.toString())
        if (address == null || (address != USB_TAG && !found.containsKey(address)))
            return problem(STEP_FIND, R.string.err_pick)
        if (address == USB_TAG)
            return sendUsb(check, ssid, psk)
        val (device, name) = found.getValue(address)
        stopScan()
        /* the board keeps no pairings (DESIGN.md 5.3): a bond from an earlier setup is stale, every send pairs anew */
        if (device.bondState != BluetoothDevice.BOND_NONE && !removeBond(device))
            return problem(STEP_SAVE, R.string.err_unpair, name)
        boardName = name
        heldFor = address
        boardId = ids[address] ?: ""
        ssidText = ssidField.text.toString()
        checked = check
        committed = false
        held = false
        val commit = if (check) COMMIT_CHECK else COMMIT_UNCHECKED
        attempt = (1..Int.MAX_VALUE).random()
        val id = ByteArray(4) { (attempt shr (8 * it)).toByte() }
        /* an open network writes no passphrase: the board starts each connection empty and its BLE stack refuses
         * zero-length writes (tests/test040) */
        planned = listOfNotNull(SSID to ssid, if (psk.isNotEmpty()) PSK to psk else null,
            COMMIT to byteArrayOf(commit) + id)
        writes = ArrayDeque(planned)
        this.device = device
        sending = true
        updateBusy()
        tries = 1
        reconciling = false
        watchAdapter()
        sendButton.isEnabled = false
        anywayOffered = false
        setupMenu()
        show(STEP_SAVE, R.string.help_connecting, name)
        armTimeout()
        fetchPin {
            watchPairingRequest()
            connectWhenUnpaired(device, SystemClock.elapsedRealtime() + UNPAIR_MS)
        }
    }

    /** the board's pairing PIN from the server (GET /v1/devices/<id>/pin): the app holds no secret; then [then] (the
     *  BLE connect, or the USB lines). No server, no setup: the board refuses a pairing or a cable setup without it */
    private fun fetchPin(then: () -> Unit) {
        val id = boardId
        pinAdded = ""
        Thread {
            val got = try {
                if (id.isEmpty()) throw IllegalStateException("no device ID advertised")
                val c = URL("${BuildConfig.SERVER}/v1/devices/$id/pin?phone=$phoneId").openConnection() as HttpURLConnection
                c.connectTimeout = 3000
                c.readTimeout = 3000
                val j = JSONObject(c.inputStream.bufferedReader().use { it.readText() })
                c.disconnect()
                j.getString("pin").takeIf { it.length == 6 && it.all(Char::isDigit) } ?: ""
            } catch (e: Exception) {
                ""
            }
            runOnUiThread {
                if (!sending)
                    return@runOnUiThread
                if (got.isEmpty())
                    return@runOnUiThread fail(R.string.err_no_pin, boardName)
                pin = got
                pinAdded = if (known().has(id)) "" else id
                then()
            }
        }.start()
    }

    /** Android asks for the passkey (LE passkey entry comes as PAIRING_VARIANT_PIN): the app enters it itself, and the
     *  user sees no PIN prompt */
    private fun watchPairingRequest() {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                @Suppress("DEPRECATION")
                val d = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE) ?: return
                if (d.address != device?.address || pin.isEmpty() ||
                    intent.getIntExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT, -1) != BluetoothDevice.PAIRING_VARIANT_PIN)
                    return
                if (!d.setPin(pin.toByteArray())) {
                    Log.w(TAG, "pairing PIN not accepted by the phone")   // the system dialog stays: the user types it
                    return
                }
                // answered: the system's PIN dialog is not needed. The request is an ordered broadcast and this
                // receiver runs before Settings (priority): stopping it keeps the dialog away (it flashed for ~2 s)
                if (isOrderedBroadcast)
                    abortBroadcast()
                Log.i(TAG, "pairing PIN entered by the app")
            }
        }
        pinReceiver = receiver
        val filter = IntentFilter(BluetoothDevice.ACTION_PAIRING_REQUEST).apply { priority = IntentFilter.SYSTEM_HIGH_PRIORITY - 1 }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED)   // the Bluetooth app's broadcast (cf. bond)
        else
            registerReceiver(receiver, filter)
    }

    private fun removeBond(d: BluetoothDevice) = try {
        d.javaClass.getMethod("removeBond").invoke(d) as Boolean    // no public API; test040's BleOp uses it too
    } catch (e: ReflectiveOperationException) {
        false
    }

    private fun connectWhenUnpaired(device: BluetoothDevice, until: Long) {
        when {
            device.bondState == BluetoothDevice.BOND_NONE -> connect()
            SystemClock.elapsedRealtime() > until -> {
                finishSend(null)
                problem(STEP_SAVE, R.string.err_unpair, boardName)
            }
            else -> main.postDelayed({ connectWhenUnpaired(device, until) }, 100)
        }
    }

    private val timeout = Runnable { fail(R.string.err_timeout) }

    private fun armTimeout() {
        main.removeCallbacks(timeout)
        main.postDelayed(timeout, SEND_TIMEOUT_MS)
    }

    /** the next STATUS read; each answer schedules the one after it (one GATT request at a time) */
    private val poll = Runnable {
        val g = gatt ?: return@Runnable
        val c = g.getService(SERVICE)?.getCharacteristic(STATUS) ?: return@Runnable
        if (!g.readCharacteristic(c))
            schedulePoll()               // the stack was busy: try again
    }

    private fun schedulePoll() {
        main.removeCallbacks(poll)
        main.postDelayed(poll, POLL_MS)
    }

    /** closes the connection and turns the buttons back on; then [done], if a send was running */
    private fun finishSend(done: (() -> Unit)?) {
        main.removeCallbacksAndMessages(null)
        bondReceiver?.let { unregisterReceiver(it) }
        pinReceiver?.let { unregisterReceiver(it) }
        pinReceiver = null
        pin = ""
        bondReceiver = null
        adapterReceiver?.let { unregisterReceiver(it) }
        adapterReceiver = null
        closeGatt()
        val wasSending = sending
        sending = false
        updateBusy()
        reconciling = false
        viaUsb = false
        usbAwait = null
        usbSteps.clear()
        usbLater.clear()
        usbSwitching = false
        sendButton.isEnabled = true
        if (wasSending) {
            held = true                  // its result stays until the next pick or send
            done?.invoke()
        }
    }

    private fun fail(res: Int, vararg args: Any) {
        // not when the board may have stored the network (the link lost after the commit): it finishes the check alone
        if (res != R.string.err_unconfirmed && res != R.string.err_usb_unconfirmed)
            takeBack()
        finishSend { problem(STEP_SAVE, res, *args); keepLooking() }
    }

    /** the PIN's fetch made this phone the board's user; a setup that failed takes that back, or the board (one of mine
     *  now) would leave Setup and the retry after a wrong password had no row (test113, 2026-11-08). Every send fetches
     *  the PIN again, Save without checking too */
    private fun takeBack() {
        val id = pinAdded
        pinAdded = ""
        if (id.isNotEmpty())
            Thread {
                try {
                    val c = URL("${BuildConfig.SERVER}/v1/devices/$id/phones/$phoneId").openConnection() as HttpURLConnection
                    c.requestMethod = "DELETE"
                    c.connectTimeout = 3000
                    c.readTimeout = 3000
                    Log.i(TAG, "setup failed: $id not added (${c.responseCode})")
                    c.disconnect()
                } catch (e: Exception) {
                    Log.w(TAG, "setup failed: $id still on the list ($e)")
                }
            }.start()
    }

    private fun closeGatt() {
        gatt?.let {
            if (permissionsGranted()) {
                it.disconnect()
                it.close()
            }
        }
        gatt = null
    }

    private fun connect() {
        val d = device ?: return
        discovered = false
        gatt = d.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        // the phone starts the pairing with every connect, retries too: with it pending the board's Security Request
        // asks the app for the PIN (no dialog: Samsung asks once, 2.3 -> 1.2 s, test106); without it Samsung shows its
        // "Pair" dialog first. A connect that timed out (status 133, the phone's 5 s) ended the pending one with it,
        // and the retry got that dialog (test106, 2026-11-02)
        if (d.bondState == BluetoothDevice.BOND_NONE)
            d.createBond()
    }

    /** a connect that failed or dropped before the commit: nothing reached the board, connect again */
    private fun retryConnect() {
        closeGatt()
        tries++
        writes = ArrayDeque(planned)
        main.postDelayed({ if (sending && gatt == null) connect() }, RETRY_MS)
    }

    /** the board stored the network: Done, then the LEAKCAMs screen with it picked */
    private fun saved() = finishSend {
        remember()
        found.clear()
        devices.removeAllViews()
        show(STEP_DONE, if (checked) R.string.help_done else R.string.help_done_unchecked, boardName, ssidText)
        // Done -> the LEAKCAMs screen at once, the board picked, the answer in its status (user 2026-11-08:
        // "once done we need to go to status screen automatically")
        if (boardId.isNotEmpty()) {
            selected = boardId
            note = Triple(boardId, getString(if (checked) R.string.done_note else R.string.done_unchecked_note,
                                             boardName, ssidText), SystemClock.elapsedRealtime() + 60_000L)
        }
        showStatus()
        loadMine()
        alertPermissions()
    }

    private val reconcileDeadline = Runnable { fail(R.string.err_unconfirmed, boardName) }

    /** the link (or Bluetooth) lost after the commit: the board may have stored the network; reconnect and read STATUS
     *  for this attempt instead of guessing */
    private fun lostAfterCommit() {
        closeGatt()
        main.removeCallbacks(timeout)
        main.removeCallbacks(poll)
        if (!reconciling) {
            reconciling = true
            main.postDelayed(reconcileDeadline, RECONCILE_MS)
            serverCheck()
        }
        firstRead = true
        writes = ArrayDeque()
        if (adapter?.isEnabled != true)
            return show(STEP_SAVE, R.string.help_bluetooth_off_reconnect, boardName)
        show(STEP_SAVE, R.string.help_reconnecting, boardName)
        main.postDelayed({ if (sending && gatt == null) connect() }, RETRY_MS)
    }

    /** while reconnecting after a lost link: the board that finished its check alone goes to the full firmware and
     *  stops advertising for setup, so the reconnect cannot find it; its check-in at the server on the network just
     *  sent, newer than the commit, is the answer (test110 2026-11-09: the app said "plug it in" for a stored network) */
    private fun serverCheck() {
        val id = boardId
        val ssid = ssidText
        if (id.isEmpty())
            return
        Thread {
            while (true) {
                if (!(reconciling && sending))   // read off the main thread: a stale read costs one more poll
                    return@Thread
                try {
                    val c = URL("${BuildConfig.SERVER}/v1/devices/$id").openConnection() as HttpURLConnection
                    c.connectTimeout = 3000
                    c.readTimeout = 3000
                    val j = if (c.responseCode == 200) JSONObject(c.inputStream.bufferedReader().readText()) else null
                    c.disconnect()
                    val since = (SystemClock.elapsedRealtime() - commitAt) / 1000
                    if (j != null && j.optBoolean("online") && j.optString("ssid") == ssid &&
                        j.optLong("last_seen_s", Long.MAX_VALUE) < since) {
                        main.post {
                            if (reconciling && sending) {
                                Log.i(TAG, "lost link: $id checked in on the new network: saved")
                                reconciling = false
                                main.removeCallbacks(reconcileDeadline)
                                saved()
                            }
                        }
                        return@Thread
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "lost link: server check ($e)")
                }
                SystemClock.sleep(1500)
            }
        }.start()
    }

    /** Bluetooth turned off during a send gives no disconnect callback (test110): watch the adapter itself */
    private fun watchAdapter() {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                when (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)) {
                    BluetoothAdapter.STATE_TURNING_OFF, BluetoothAdapter.STATE_OFF ->
                        if (committed) lostAfterCommit() else fail(R.string.err_bluetooth_off)
                    BluetoothAdapter.STATE_ON -> if (reconciling && gatt == null) lostAfterCommit()
                }
            }
        }
        adapterReceiver = receiver
        registerReceiver(receiver, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED))
    }

    /** a pairing that did not complete: declined, or the board on battery */
    private fun pairingFailed() = fail(R.string.err_pairing_refused)

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, st: Int, newState: Int) {
            main.post {
                if (g != gatt) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    // short connection interval for the few seconds of setup: at the phone's 45 ms the discovery,
                    // the writes and each check step's notification waited on it (test106)
                    g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                    if (!g.requestMtu(MTU))
                        pairThenDiscover(g)
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    when {
                        committed -> lostAfterCommit()
                        bondReceiver != null -> pairingFailed()                  // dropped while pairing
                        tries < CONNECT_TRIES -> retryConnect()
                        else -> fail(R.string.err_disconnected, st)
                    }
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, st: Int) {
            main.post { if (g == gatt) pairThenDiscover(g) }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, st: Int) {
            main.post {
                if (g != gatt) return@post
                discovered = true
                startWrites(g)
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, st: Int) {
            main.post {
                if (g != gatt) return@post
                when (st) {
                    BluetoothGatt.GATT_SUCCESS -> writeNext(g)
                    BluetoothGatt.GATT_INSUFFICIENT_AUTHENTICATION, BluetoothGatt.GATT_INSUFFICIENT_ENCRYPTION -> pairingFailed()
                    else -> fail(R.string.err_no_service)
                }
            }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            main.post { if (g == gatt && c.uuid == STATUS) onStatus(value) }
        }

        @Deprecated("Android 12 and older")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            val value = c.value ?: return
            main.post { if (g == gatt && c.uuid == STATUS) onStatus(value) }
        }

        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray, st: Int) {
            main.post { if (g == gatt && c.uuid == STATUS) onRead(value, st) }
        }

        @Deprecated("Android 12 and older")
        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, st: Int) {
            @Suppress("DEPRECATION")
            val value = c.value ?: ByteArray(0)
            main.post { if (g == gatt && c.uuid == STATUS) onRead(value, st) }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, st: Int) {
            main.post {
                if (g != gatt) return@post
                when {
                    st == BluetoothGatt.GATT_SUCCESS -> writeNext(g)
                    st == BluetoothGatt.GATT_INSUFFICIENT_AUTHENTICATION || st == BluetoothGatt.GATT_INSUFFICIENT_ENCRYPTION -> pairingFailed()
                    c.uuid == COMMIT -> lostAfterCommit()     // the board may have taken it: read STATUS for it
                    else -> fail(R.string.err_refused, what(c.uuid), st)
                }
            }
        }
    }

    /** the board asks for encryption on connect: discovery runs meanwhile (it needs none; after the bond it cost
     *  0.4 s, test106), the writes wait for both */
    private fun pairThenDiscover(g: BluetoothGatt) {
        val device = g.device
        discovered = false
        bonded = device.bondState == BluetoothDevice.BOND_BONDED
        g.discoverServices()
        if (bonded) {
            if (!reconciling)
                show(STEP_SAVE, R.string.help_sending)      // paired already while the MTU was asked
            return
        }
        show(STEP_SAVE, R.string.help_pairing, boardName)
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                @Suppress("DEPRECATION")
                val d = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                if (d?.address != device.address || g != gatt) return
                when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)) {
                    BluetoothDevice.BOND_BONDED -> {
                        unregisterReceiver(this)
                        bondReceiver = null
                        bonded = true
                        if (!reconciling)
                            show(STEP_SAVE, R.string.help_sending)
                        startWrites(g)
                    }
                    BluetoothDevice.BOND_NONE -> pairingFailed()
                }
            }
        }
        bondReceiver = receiver
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            /* exported: the bond broadcast comes from the Bluetooth app's uid, and a NOT_EXPORTED receiver never got it
             * (Samsung, Android 13, test106 2026-10-30); only the system may send this protected broadcast */
            registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED)
        else
            registerReceiver(receiver, filter)
        if (device.bondState == BluetoothDevice.BOND_NONE)
            device.createBond()
    }

    private fun startWrites(g: BluetoothGatt) {
        if (!discovered || !bonded)
            return
        if (g.getService(SERVICE) == null) fail(R.string.err_no_service) else subscribe(g)
    }

    /** STATUS notifications on, before anything is written: the check reports through them */
    private fun subscribe(g: BluetoothGatt) {
        val c = g.getService(SERVICE)?.getCharacteristic(STATUS) ?: return fail(R.string.err_no_service)
        val d = c.getDescriptor(CCC) ?: return fail(R.string.err_no_service)
        g.setCharacteristicNotification(c, true)
        val value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeDescriptor(d, value) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            d.value = value
            @Suppress("DEPRECATION")
            g.writeDescriptor(d)
        }
        if (!started)
            fail(R.string.err_no_service)
    }

    private fun onRead(v: ByteArray, st: Int) {
        if (st == BluetoothGatt.GATT_SUCCESS)
            onStatus(v)
        if (gatt != null)
            schedulePoll()
    }

    /** the board's check, from a notification or a read alike: progress, then SAVED or a failure (nothing stored) */
    private fun onStatus(v: ByteArray) {
        if (!committed || v.isEmpty()) return
        val state = v[0].toInt() and 0xff
        val detail = if (v.size >= 3) (v[1].toInt() and 0xff) or ((v[2].toInt() and 0xff) shl 8) else 0
        val id = if (v.size >= 7) (3..6).fold(0) { a, i -> a or ((v[i].toInt() and 0xff) shl (8 * (i - 3))) } else 0
        if (id != attempt) {
            if (reconciling && firstRead)        // after a lost link: the board never took this commit, or restarted
                fail(if (id == 0 && state == WCHK_IDLE) R.string.err_unconfirmed else R.string.err_not_sent, boardName)
            return
        }
        if (reconciling) {                       // reconnected: the board answers for this attempt
            reconciling = false
            main.removeCallbacks(reconcileDeadline)
            lastState = -1
        }
        firstRead = false
        if (state == WCHK_IDLE || state == lastState) return
        lastState = state
        armTimeout()
        when (state) {
            WCHK_JOINING -> show(STEP_SAVE, R.string.help_check_join, ssidText)
            WCHK_ADDRESS -> show(STEP_SAVE, R.string.help_check_address)
            WCHK_INTERNET -> show(STEP_SAVE, R.string.help_check_internet)
            WCHK_SAVED -> saved()
            else -> {
                // where the board is now may be the only problem: offer to store unchecked
                val placeOnly = state in setOf(WCHK_NOT_FOUND, WCHK_NO_ADDRESS, WCHK_NO_DNS, WCHK_NO_INTERNET, WCHK_CAPTIVE, WCHK_NO_API)
                takeBack()                // nothing stored: the board stays new
                finishSend {
                    when (state) {
                        WCHK_NOT_FOUND -> problem(STEP_WIFI, R.string.chk_not_found, ssidText)
                        WCHK_WRONG_PASSWORD -> problem(STEP_WIFI, R.string.chk_wrong_password, ssidText, detail)
                        WCHK_JOIN_FAILED -> problem(STEP_SAVE, R.string.chk_join_failed, detail)
                        WCHK_NO_ADDRESS -> problem(STEP_SAVE, R.string.chk_no_address)
                        WCHK_NO_DNS -> problem(STEP_SAVE, R.string.chk_no_dns)
                        WCHK_NO_INTERNET -> problem(STEP_SAVE, R.string.chk_no_internet)
                        WCHK_CAPTIVE -> problem(STEP_WIFI, R.string.chk_captive, detail)
                        WCHK_BUSY -> problem(STEP_SAVE, R.string.chk_busy)
                        WCHK_SAVE_FAILED -> problem(STEP_SAVE, R.string.chk_save_failed)
                        WCHK_NO_API -> problem(STEP_SAVE, R.string.chk_no_api, detail)
                        else -> problem(STEP_SAVE, R.string.chk_unknown, state)
                    }
                    anywayOffered = placeOnly
                    setupMenu()
                    Log.i(TAG, "anyway offered ${if (placeOnly) 1 else 0}")   // BleOp: the menu item to press
                }
            }
        }
    }

    private fun writeNext(g: BluetoothGatt) {
        val next = writes.removeFirstOrNull()
        if (next == null) {
            if (committed)
                schedulePoll()           // the board reports on STATUS: notified, and read as well
            return
        }

        val (uuid, value) = next
        if (uuid == COMMIT) {
            committed = true             // STATUS may answer before the write does (save, busy)
            commitAt = SystemClock.elapsedRealtime()
            lastState = -1
        }
        val c = g.getService(SERVICE)?.getCharacteristic(uuid) ?: return fail(R.string.err_no_service)
        val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(c, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            c.value = value
            @Suppress("DEPRECATION")
            g.writeCharacteristic(c)
        }
        if (!started)
            fail(R.string.err_write, what(uuid))
    }

    // ---------------- the USB cable ----------------

    /** the cable screen: the board found by the plug, no list, no steps; the Wi-Fi fields open at once without a
     *  stored network, else what it has and how to change it */
    /** the board on the cable as the one picked in the setup list (once): the cable first (DESIGN.md 5.3.4), the
     *  list adjusted on the fly; one of mine is not listed (new devices only, user 2026-11-07) */
    private fun addUsbRadio() {
        if (known().has(usbId))
            return
        if (devices.findViewWithTag<View>(USB_TAG) == null)
            devices.addView(RadioButton(this).apply {
                id = View.generateViewId()
                tag = USB_TAG
                text = usbId
                setCompoundDrawablesRelative(null, null, iconStrip(usbId, USB_TAG), null)
            }, 0)
        devices.check(devices.findViewWithTag<View>(USB_TAG).id)
        updateBusy()
    }

    private fun onUsbUp(up: Boolean) {
        usbTimer.removeCallbacksAndMessages(null)
        if (up) {
            usbIdTries = 0
            return askId(0)
        }
        if (sending && viaUsb && usbSwitching) {
            Log.i(TAG, "usb board $usbId switching to its setup firmware")   // test113
            usbId = ""                   // its ID answered again on the way back
            return
        }
        if (sending && viaUsb)
            fail(if (committed) R.string.err_usb_unconfirmed else R.string.err_usb_gone, boardName)
        Log.i(TAG, "usb board ${usbId.ifEmpty { "-" }} gone")
        usbId = ""
        ids.remove(USB_TAG)
        stored.remove(USB_TAG)
        devices.findViewWithTag<View>(USB_TAG)?.let {
            if (it.id == devices.checkedRadioButtonId)
                devices.clearCheck()            // the cable's board gone: looking again
            devices.removeView(it)
        }
        updateBusy()
        if (!onStatus && !sending && step != STEP_DONE && !held)   // Done stays: the board ends the link itself after it
            wifiHelp()
    }

    /** the K230's relay starts with its program (~0.6 s after its boot), maybe after the USB device: ask until answered */
    private fun askId(n: Int) {
        usbIdTries = n
        if (usbId.isNotEmpty() || !usbLink.up || n >= USB_ID_TRIES)
            return                           // answered, gone, or never answering
        usbLink.write("ID")
        usbTimer.postDelayed({ askId(n + 1) }, 1_000L)
    }

    /** a line from the board: the ID answer, a STATUS (pushed on every change, also before a command's OK), or the
     *  answer to the command sent last */
    private fun onUsbLine(line: String) {
        val p = line.split(' ')
        when {
            p[0] == "ID" && p.size >= 3 -> {
                if (usbSwitching && sending && viaUsb) {   // back from the switch: the setup firmware's lines now
                    usbId = p[1]
                    if (p.getOrNull(5) == "full")
                        return                       // the switch failed, the full firmware back: the timeout says so
                    usbSwitching = false
                    usbFull = false
                    Log.i(TAG, "usb board $usbId on its setup firmware")   // test113
                    armTimeout()
                    usbSteps = ArrayDeque(listOf("PIN $pin") + usbLater)
                    usbLater.clear()
                    return usbNext()
                }
                if (usbId == p[1])
                    return
                usbId = p[1]
                usbStored = p[2] == "1"
                usbFull = p.getOrNull(5) == "full"   // ID <id> <stored> <mV> <USB power> <firmware>; older: setup
                ids[USB_TAG] = usbId
                stored[USB_TAG] = usbStored
                if (p.size >= 5) {                   // ID <id> <stored> <battery mV, -1 none> <USB power>
                    p[3].toIntOrNull()?.takeIf { it >= 0 }?.let { batteryMv[p[1]] = it }
                    charging[p[1]] = p[4] == "1"
                }
                Log.i(TAG, "usb board $usbId creds ${if (usbStored) 1 else 0}")   // test113
                // its row added, picked (the list adjusted on the fly), never over a setup that runs (the board
                // just set up is one of mine now: not listed)
                if (!onStatus && !sending)
                    addUsbRadio()
            }
            p[0] == "STATUS" && p.size >= 4 -> {
                if (!sending || !viaUsb)
                    return
                val st = p[1].toIntOrNull() ?: return
                val d = p[2].toIntOrNull() ?: 0
                val a = p[3].toLongOrNull()?.toInt() ?: 0
                onStatus(byteArrayOf(st.toByte(), d.toByte(), (d shr 8).toByte(),
                    a.toByte(), (a shr 8).toByte(), (a shr 16).toByte(), (a shr 24).toByte()))
            }
            sending && viaUsb && usbAwait != null -> {
                val cmd = usbAwait!!
                usbAwait = null
                when (line) {
                    "OK" -> usbNext()
                    "ERR PIN" -> fail(R.string.err_usb_pin, boardName)
                    "ERR LOCKED" -> fail(R.string.err_usb_locked, boardName)
                    else -> fail(R.string.err_usb_refused, boardName, getString(when (cmd) {
                        "SSID" -> R.string.what_ssid
                        "PSK" -> R.string.what_psk
                        "SETUP" -> R.string.what_setup
                        else -> R.string.what_commit
                    }), line)
                }
            }
        }
    }

    /** the same check as over BLE, in lines: PIN, SSID, PSK (none for an open network), COMMIT; then STATUS reports.
     *  On the full firmware (a set-up board, DESIGN.md 5.3.4): PIN, SETUP, the switch, then those lines */
    private fun sendUsb(check: Boolean, ssid: ByteArray, psk: ByteArray) {
        if (!usbLink.up || usbId.isEmpty())
            return problem(STEP_FIND, R.string.err_usb_gone, usbId)
        stopScan()
        boardName = usbId
        heldFor = USB_TAG
        boardId = usbId
        ssidText = ssidField.text.toString()
        checked = check
        committed = false
        attempt = (1..Int.MAX_VALUE).random()
        viaUsb = true
        sending = true
        updateBusy()
        sendButton.isEnabled = false
        anywayOffered = false
        setupMenu()
        show(STEP_SAVE, R.string.help_usb_sending, boardName)
        armTimeout()
        val hex = { b: ByteArray -> b.joinToString("") { "%02x".format(it) } }
        val lines = listOfNotNull("SSID ${hex(ssid)}", if (psk.isNotEmpty()) "PSK ${hex(psk)}" else null,
            "COMMIT ${if (check) 1 else 2} $attempt")
        fetchPin {
            usbSteps = ArrayDeque(listOf("PIN $pin") + if (usbFull) listOf("SETUP") else lines)
            usbLater = ArrayDeque(if (usbFull) lines else listOf())
            usbNext()
        }
    }

    private fun usbNext() {
        val l = usbSteps.removeFirstOrNull() ?: return
        val cmd = l.substringBefore(' ')
        if (cmd == "COMMIT") {
            committed = true             // its first STATUS comes before its OK
            lastState = -1
        }
        if (cmd == "SETUP") {
            usbSwitching = true          // its OK, then the link goes: not a pull
            armTimeout()
        }
        usbAwait = cmd
        if (!usbLink.write(l))
            fail(R.string.err_usb_gone, boardName)
    }

    // ---------------- the LEAKCAMs set up from this phone ----------------

    private fun known() = mine

    /** this phone's boards from the server (GET /v1/phones/<phone>/devices): the list and each one's presence and
     *  network. No server: the list unchanged, said so per row */
    private fun loadMine() {
        Thread {
            val list = try {
                val c = URL("${BuildConfig.SERVER}/v1/phones/$phoneId/devices").openConnection() as HttpURLConnection
                c.connectTimeout = 3000
                c.readTimeout = 3000
                val a = org.json.JSONArray(c.inputStream.bufferedReader().use { it.readText() })
                c.disconnect()
                (0 until a.length()).map { a.getJSONObject(it) }
            } catch (e: Exception) {
                null
            }
            runOnUiThread {
                if (list != null) {
                    mine = JSONObject().apply { for (j in list) put(j.getString("id"), JSONObject()) }
                    for (j in list) {
                        online[j.getString("id")] = j.getBoolean("online")
                        j.optInt("battery_mv", -1).takeIf { it >= 0 }?.let { batteryMv[j.getString("id")] = it }
                        if (j.has("charging"))
                            charging[j.getString("id")] = j.getBoolean("charging")
                        j.optString("ssid").takeIf { it.isNotEmpty() }?.let { ssids[j.getString("id")] = it }
                        nextAt[j.getString("id")] = j.optLong("next_checkin", 0)
                        pend[j.getString("id")] = j.optJSONArray("pending") ?: org.json.JSONArray()
                        medias[j.getString("id")] = j.optJSONArray("media") ?: org.json.JSONArray()
                        focusOf(j)?.let { focusCam[j.getString("id")] = it } ?: focusCam.remove(j.getString("id"))
                    }
                }
                rows = (list?.map { j -> Triple(j.getString("id"), j.getString("id"), line(j)) }
                        ?: mine.keys().asSequence().map { Triple(it, it, getString(R.string.mine_unknown, it)) }.toList())
                for (r in rows)
                    Log.i(TAG, "mine ${r.first}: ${r.third}")
                // an empty status list is no screen: straight to the setup (user 2026-11-07)
                if (onStatus && mine.length() == 0)
                    openSetup(null)
                else if (onStatus)
                    render()
            }
        }.start()
    }

    private fun line(j: JSONObject): String {
        val name = j.getString("id")
        val seen = j.getLong("last_seen_s")
        val ssid = j.optString("ssid")
        val state = when {
            j.getBoolean("online") -> getString(R.string.mine_online, name)
            seen < 0 -> getString(R.string.mine_never, name)
            seen < 60 -> getString(R.string.mine_offline_s, name, seen.toInt())
            else -> getString(R.string.mine_offline, name, (seen / 60).toInt())
        }
        // its next check-in always said (user 2026-11-07): when a photo, a video or a Wi-Fi change reaches it
        return getString(R.string.mine_next, getString(R.string.mine_ssid, state, ssid.ifEmpty { getString(R.string.mine_ssid_unknown) }),
                         checkinAt(j.optLong("next_checkin", 0))) + room(j)
    }

    /** the camera to tune, from the last leak check's "<cam0>,<cam1>,<foc0>,<foc1>" (DESIGN.md 5.3.17): one out of
     *  focus, else one never tuned (-1); null: both fine, or an older board */
    private fun focusOf(j: JSONObject): Int? {
        val f = j.optJSONObject("metrics")?.optString("job").orEmpty().split(",")
        if (f.size < 4)
            return null
        return (0..1).firstOrNull { f[it] == "unfocused" } ?: (0..1).firstOrNull { f[2 + it] == "-1" && f[it] != "nocam" }
    }

    /** the room's temperature and humidity (DESIGN.md 5.3.15): only a reading the board took after 3 h cool, with its
     *  age; none yet: nothing said */
    private fun room(j: JSONObject): String {
        val m = j.optJSONObject("metrics") ?: return ""
        if (!m.has("troom") || !m.has("rhroom"))
            return ""
        val t = m.getInt("troom") / 10.0
        val rh = m.getInt("rhroom") / 10.0
        val ago = m.optLong("roomago", -1)
        val seen = j.optLong("last_seen_s", -1)
        val h = if (ago >= 0 && seen >= 0) (ago + seen) / 3600 else -1
        val what = getString(R.string.mine_room, t, rh)
        return " · " + if (h >= 1) getString(R.string.mine_room_ago, what, h.toInt()) else what
    }

    /** the delete goes to the server (DELETE /v1/devices/<id>/phones/<phone>): this phone no longer a user; then the
     *  server's list again. No server: nothing changes, said so */
    private fun forget(id: String) {
        AlertDialog.Builder(this)
            .setMessage(getString(R.string.delete_ask, id))
            .setPositiveButton(R.string.delete) { _, _ ->
                Thread {
                    val ok = try {
                        val c = URL("${BuildConfig.SERVER}/v1/devices/$id/phones/$phoneId").openConnection() as HttpURLConnection
                        c.requestMethod = "DELETE"
                        c.connectTimeout = 3000
                        c.readTimeout = 3000
                        val code = c.responseCode
                        c.disconnect()
                        code == 200 || code == 404     // 404: not a user any more (deleted elsewhere)
                    } catch (e: Exception) {
                        false
                    }
                    runOnUiThread {
                        if (!ok)
                            return@runOnUiThread android.widget.Toast.makeText(this, getString(R.string.err_delete, id),
                                android.widget.Toast.LENGTH_LONG).show()
                        Log.i(TAG, "deleted $id")
                        loadMine()           // the last one deleted: the setup screen
                    }
                }.start()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** the board just stored the network: kept by its ID, then its presence asked */
    private fun remember() {
        if (boardId.isEmpty())
            return
        mine.put(boardId, JSONObject())  // registered at the server when it issued the PIN; its list confirms it
    }


    /** the list and each board's presence, from the server; logged for tests/test111 */
    private fun refreshPresence() = loadMine()

    private fun what(uuid: UUID) = getString(when (uuid) {
        SSID -> R.string.what_ssid
        PSK -> R.string.what_psk
        else -> R.string.what_commit
    })
}
