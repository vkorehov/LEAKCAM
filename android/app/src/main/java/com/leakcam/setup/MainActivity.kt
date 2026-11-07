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
        const val SCAN_MS = 10_000L           // the longest Find: no board by then
        const val SEND_TIMEOUT_MS = 60_000L   // per step: pairing (the user reads and accepts), each check step
        const val POLL_MS = 1_000L            // STATUS read while the check runs: a notification can be lost (test106)
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
        const val DECIDE_MS = 5_000L          // the start looks on the cable and over Bluetooth this long, then the
                                              // cable wins (DESIGN.md 5.3.4: USB > BLE; user 2026-11-07)
        // the status screen's periods; a debug build on the bench runs them 3x faster (`adb shell settings put global
        // leakcam_fast_timing 1`): the tests wait for the app's refreshes, not real-time (2026-11-07). Android drops
        // scans started more than 5 in 30 s: the fast scan stays at one per 9 s
        const val TICK_MS = 10_000L           // the server's presence read
        const val RESCAN_MS = 30_000L         // no board found: the next scan
        const val UNSEEN_MS = 45_000L         // a board not in a scan for this long leaves the status list
    }

    private lateinit var steps: List<TextView>
    private lateinit var stepTexts: List<String>
    private lateinit var help: TextView
    private lateinit var sendButton: Button
    private lateinit var setupMore: android.widget.ImageButton
    // the setup screen's rare actions (its three-dots menu): Save without checking after a check that may have failed
    // only where the board is now; back to the status list when this phone has boards
    private var anywayOffered = false
    private var backOffered = false
    private lateinit var devices: RadioGroup
    private lateinit var ssidField: EditText
    private lateinit var passwordField: EditText
    private lateinit var statusPanel: View
    private lateinit var setupPanel: View
    private lateinit var mineList: LinearLayout
    private lateinit var nearby: TextView
    private lateinit var setupOpen: Button
    private lateinit var more: android.widget.ImageButton
    private lateinit var takePhoto: Button
    private lateinit var takeVideo: Button

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
    private val rssi = HashMap<String, Int>()   // address -> the last advertisement's signal (dBm)
    private val ticker = Handler(Looper.getMainLooper())   // the status screen's refresh; main's callbacks get cleared
    private var onStatus = false
    private var changeOnly = false       // setup of a known LEAKCAM: only its Wi-Fi
    private val ssids = HashMap<String, String>()   // device ID -> the network it checked in from (server; not kept)
    /* the server is the one source of truth (DESIGN.md 5.3.5): this phone's boards are the server's list, held here
     * only in memory, never stored on the phone */
    private var mine = JSONObject()
    private val phoneId by lazy {                     // the phone as the server knows its user: its Android ID
        android.provider.Settings.Secure.getString(contentResolver, android.provider.Settings.Secure.ANDROID_ID).orEmpty()
    }
    private var selected = ""                        // the LEAKCAM picked on the status screen (radio)
    private var rows = listOf<Triple<String, String, String>>()   // the remembered ones: ID, name, presence line
    private val online = HashMap<String, Boolean>()
    private var scannedOnStatus = false   // a status scan finished since the list opened: "not nearby" is known
    private val waking = HashMap<String, Pair<Long, Int>>()   // ID -> the wake asked (elapsedRealtime ms), within s   // device ID -> the server sees it online (a Wi-Fi icon)
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
    private var firstRead = false        // the first STATUS after a reconnect decides
    private var adapterReceiver: BroadcastReceiver? = null
    private var ssidText = ""
    private var writes = ArrayDeque<Pair<UUID, ByteArray>>()
    private var bondReceiver: BroadcastReceiver? = null
    private var pinReceiver: BroadcastReceiver? = null
    private var pin = ""                 // this send's pairing PIN, from the server (never stored)
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
    private var usbFlow = false          // the cable screen is open
    private var usbIdTries = 0           // IDs asked on this link
    private var starting = true          // the start's spinner: waiting for the cable, the server's list
    private lateinit var usbHeader: TextView

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
        help = findViewById(R.id.help)
        usbHeader = findViewById(R.id.usb_header)
        statusPanel = findViewById(R.id.status_panel)
        setupPanel = findViewById(R.id.setup_panel)
        mineList = findViewById(R.id.mine_list)
        nearby = findViewById(R.id.nearby)
        setupOpen = findViewById(R.id.setup_open)
        more = findViewById(R.id.more)
        takePhoto = findViewById(R.id.take_photo)
        takeVideo = findViewById(R.id.take_video)
        takePhoto.setOnClickListener { capture(selected, "photo") }
        takeVideo.setOnClickListener { capture(selected, "video") }
        // the picked board's rare actions, one menu next to its action buttons (user 2026-11-07: no button or menu
        // in the rows; Change Wi-Fi is rare too, photo and video are the common actions)
        more.setOnClickListener { v ->
            val id = selected
            android.widget.PopupMenu(this, v).apply {
                menu.add(changeLabel).apply { isEnabled = changeEnabled }.setOnMenuItemClickListener {
                    addressOf(id)?.let { openBoard(it) } ?: wake(id); true
                }
                if (deletable)
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
        devices.setOnCheckedChangeListener { _, _ -> if (!scanning && gatt == null && !sending) wifiHelp() }
        sendButton.setOnClickListener { if (ready()) send(true) }
        setupMore.setOnClickListener { v ->
            android.widget.PopupMenu(this, v).apply {
                if (anywayOffered)
                    menu.add(R.string.save_anyway).setOnMenuItemClickListener { if (ready()) send(false); true }
                if (backOffered)
                    menu.add(R.string.to_status).setOnMenuItemClickListener { if (!sending) showStatus(); true }
                show()
            }
        }
        setupOpen.setOnClickListener { addressOf(selected)?.let { openBoard(it) } }
        deleteSharedPreferences("devices")    // the list an older version kept: the server holds it now (DESIGN.md 5.3.5)
        Log.i(TAG, "phone $phoneId")      // the tests register this phone at the server by it
        PushService.register(this)        // the server can push the leak alert to this phone (DESIGN.md 5.3.13)
        // the list at once, looking over Bluetooth, the cable in parallel (user 2026-11-07: no USB wait at every start):
        // a LEAKCAM answering on the cable takes the screen (onUsbLine); the server's list fills in when it comes
        usbLink = UsbLink(this, ::onUsbLine, ::onUsbUp)
        usbLink.start()
        loadMine { if (onStatus) render() }
        showStatus()                       // looking over Bluetooth at once; the cable's answer decides at DECIDE_MS
        usbTimer.postDelayed({ startDecide() }, DECIDE_MS)
        if (intent.hasExtra(LeakAlert.EXTRA_ID))
            showLeak(intent)               // started by a leak alert: its leak panel
    }

    /** the start's decision after DECIDE_MS of looking both ways: a LEAKCAM on the cable is picked in the list (the
     *  cable first, DESIGN.md 5.3.4), the screen only adjusted, never switched (user 2026-11-07) */
    private fun startDecide() {
        if (!starting)
            return
        starting = false
        if (usbId.isEmpty()) {
            Log.i(TAG, "start: no LEAKCAM on the cable")   // test113
            return
        }
        selected = usbId
        if (onStatus) render()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (intent.hasExtra(LeakAlert.EXTRA_ID)) {
            showLeak(intent)
            return
        }
        usbLink.find()                   // opened again by a LEAKCAM plugged in (singleTop)
    }

    /** opened from a leak alert ("I see it"): that board picked on the status screen with its leak panel on top:
     *  which board, when, what found it, and its record (4 pictures, a 15 s video) as the board uploads it */
    private fun showLeak(i: Intent) {
        val id = i.getStringExtra(LeakAlert.EXTRA_ID) ?: return
        selected = id
        showStatus()
        findViewById<View>(R.id.leak_panel).visibility = View.VISIBLE
        findViewById<TextView>(R.id.leak_title).text = i.getStringExtra(LeakAlert.EXTRA_TITLE) ?: getString(R.string.alert_title, id)
        findViewById<TextView>(R.id.leak_text).text = i.getStringExtra(LeakAlert.EXTRA_TEXT).orEmpty()
        // one tile per picture and video ID the push announced (pictures in a row, the video under them), a spinner each
        // until the gallery has downloaded it by its ID (DESIGN.md 5.3.13)
        val ids = i.getStringExtra(LeakAlert.EXTRA_MEDIA).orEmpty().split(",").filter { it.isNotBlank() }
        // one preview: the first file downloaded (a spinner until then), "+N more" for the others; a tap opens them
        // all in the phone's gallery (user 2026-11-07)
        val preview = findViewById<android.widget.FrameLayout>(R.id.leak_preview)
        val more = findViewById<TextView>(R.id.leak_more)
        preview.visibility = if (ids.isNotEmpty()) View.VISIBLE else View.GONE
        more.visibility = if (ids.size > 1) View.VISIBLE else View.GONE
        more.text = getString(R.string.leak_more, ids.size - 1)
        var shown = false
        for (mid in ids)
            gallery.fetch(mid, preview) { _, thumb ->
                if (!shown) {
                    shown = true
                    val video = mid.substringAfterLast('-').startsWith("v")
                    gallery.show(preview, video, thumb)
                    preview.setOnClickListener { gallery.open() }
                }
            }
        Log.i(TAG, "leak panel $id")
    }

    override fun onDestroy() {
        usbTimer.removeCallbacksAndMessages(null)
        usbLink.stop()
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
        help.text = text
        // each screen change in the log: setup timings (tests/test106 timeline.py) and support; no passphrase in any text
        Log.i(TAG, "step $current${if (error) " error" else ""}: ${text.lineSequence().first()}")
        help.setBackgroundColor(getColor(if (error) R.color.error_background else R.color.help_background))
    }

    private fun show(current: Int, res: Int, vararg args: Any) = showText(current, getString(res, *args), false)

    private fun problem(current: Int, res: Int, vararg args: Any) = showText(current, getString(res, *args), true)

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
    private var changeLabel = ""          // the menu's Change Wi-Fi item for the picked board, as render() left it
    private var changeEnabled = false
    private var deletable = false

    /** Take photo / Take video (DESIGN.md 5.3.14): the request to the server, handed over at the board's next check-in;
     *  the app is done at once and says when it happens (the 15 s rule); a push comes when it is ready to play */
    private fun capture(id: String, kind: String) {
        val what = getString(if (kind == "video") R.string.kind_video else R.string.kind_photo)
        Thread {
            val within = try {
                val c = URL("${BuildConfig.SERVER}/v1/devices/$id/capture").openConnection() as HttpURLConnection
                c.requestMethod = "POST"
                c.doOutput = true
                c.connectTimeout = 5000
                c.readTimeout = 5000
                c.setRequestProperty("Content-Type", "application/json")
                c.outputStream.use { it.write(JSONObject().put("kind", kind).toString().toByteArray()) }
                val s = if (c.responseCode == 200) JSONObject(c.inputStream.bufferedReader().readText()).optInt("within_s", -1) else -1
                c.disconnect()
                s
            } catch (e: Exception) {
                -1
            }
            Log.i(TAG, "capture $kind $id within $within")   // test: the request reached the server
            runOnUiThread {
                // kept over the status screen's refreshes for a minute (the presence redraw replaced it at once)
                note = Triple(id, if (within < 0) getString(R.string.capture_failed)
                                  else getString(R.string.capture_later, id, what,
                                                 if (within >= 120) "${within / 60} min" else "$within s"),
                              SystemClock.elapsedRealtime() + 60_000L)
                nearby.text = note?.second
            }
        }.start()
    }

    /** the setup of a board in reach: over the cable when it is on it, else over Bluetooth */
    private fun openBoard(address: String) = if (address == USB_TAG) openUsb() else openSetup(address)

    /** the setup screen's three dots: there (keeping its place when empty) only when it has an item */
    private fun setupMenu() {
        setupMore.visibility = if (anywayOffered || backOffered) View.VISIBLE else View.INVISIBLE
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
        if (askingAlert)
            alertPermissions()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        if (requestCode == REQUEST_NOTIFY) {
            alertPermissions()
            return
        }
        if (requestCode != REQUEST_PERMISSIONS)
            return
        if (permissionsGranted()) show(step, R.string.err_permission_granted) else problem(step, R.string.err_permission)
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

    private fun startScan() {
        val scanner = adapter?.bluetoothLeScanner ?: return problem(STEP_FIND, R.string.err_bluetooth_off)
        stopScan()
        if (onStatus) {
            // the status list keeps a board while it was seen in the last 45 s (scans every 30 s): its Bluetooth icon
            // goes when it is out of range, not at every rescan (test118)
            val now = SystemClock.elapsedRealtime()
            for (a in found.keys.filter { now - (seen[it] ?: 0L) > UNSEEN_MS / timeScale }) {
                found.remove(a)
                devices.findViewWithTag<View>(a)?.let { devices.removeView(it) }
            }
        } else {
            found.clear()
            devices.removeAllViews()
            if (usbId.isNotEmpty())
                addUsbRadio()
        }
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        scanning = true
        sendButton.isEnabled = false
        show(STEP_FIND, R.string.help_scanning)
        main.postDelayed({ scanDone() }, SCAN_MS)
    }

    private fun scanDone() {
        stopScan()
        if (onStatus) {
            scannedOnStatus = true
            return render()
        }
        if (found.isEmpty()) {
            problem(STEP_PLUG,
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) R.string.help_none_found_location
                else R.string.help_none_found)
            // again in 30 s until one shows up: a board on battery advertises only in windows, and Android silently
            // drops a scan when the app started more than 5 in 30 s (test111, "scanning too frequently")
            main.postDelayed({ if (!onStatus && !sending && !scanning && found.isEmpty()) startScan() }, RESCAN_MS / timeScale)
        }
    }

    private fun stopScan() {
        main.removeCallbacksAndMessages(null)
        if (scanning && permissionsGranted())
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        scanning = false
        sendButton.isEnabled = true
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

    /** a setup list radio: "serial · SSID" and its icons, as the status list shows the board */
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
        if (found.containsKey(d.address)) {
            // the same row refreshed (its bars, power, battery) without a redraw of the list
            devices.findViewWithTag<RadioButton>(d.address)?.let { rb ->
                rb.setCompoundDrawablesRelative(null, null, iconStrip(ids[d.address] ?: "", d.address), null)
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
        stored[d.address] = mfg != null && mfg.size >= 7 && (mfg[6].toInt() and 1) != 0
        Log.i(TAG, "found $name id ${devId ?: "none"} creds ${if (stored[d.address] == true) 1 else 0}")   // test111
        found[d.address] = d to name
        devices.addView(RadioButton(this).apply {
            id = View.generateViewId()
            tag = d.address
            text = deviceText(name)   // no "set up before" label (user 2026-11-04)
            setCompoundDrawablesRelative(null, null, iconStrip(name, d.address), null)
            setOnLongClickListener { showDbm(d.address); true }
        })
        // the first board leads on at once, Find goes on behind (another board nearby joins the list) until Send:
        // waiting for the whole Find took 10 s of the setup, then 1.7 s (test106)
        if (onStatus)
            return render()
        when (found.size) {
            1 -> {
                devices.check(devices.getChildAt(0).id)
                sendButton.isEnabled = true
                wifiHelp()
            }
            2 -> show(STEP_WIFI, R.string.help_pick)
        }
    }

    private fun wifiHelp() = when {
        changeOnly -> show(STEP_WIFI, R.string.help_change, boardLabel())
        pickedTag() == USB_TAG -> show(STEP_WIFI, R.string.help_usb_wifi, usbId)
        else -> show(STEP_WIFI, R.string.help_wifi)
    }

    private fun pickedTag() = devices.findViewById<RadioButton>(devices.checkedRadioButtonId)?.tag as String?

    private fun boardLabel() = if (pickedTag() == USB_TAG) usbId else found.values.firstOrNull()?.second.orEmpty()

    // ---------------- the status screen ----------------

    /** the LEAKCAMs set up from this phone and their presence, refreshed every 10 s; a scan for one on USB power */
    private fun showStatus() {
        onStatus = true
        usbFlow = false
        scannedOnStatus = false
        setupPanel.visibility = View.GONE
        statusPanel.visibility = View.VISIBLE
        shown = ""                      // back on the list: drawn again
        render()
        ticker.removeCallbacksAndMessages(null)
        tick(0)
    }

    private fun tick(n: Int) {
        if (!onStatus)
            return
        refreshPresence()
        // every 30 s: a LEAKCAM plugged in shows up, one unplugged goes; every 10 s while a woken one is awaited
        if ((n % 3 == 0 || waking.keys.any { wakeWaiting(it) }) && !scanning && permissionsGranted() &&
            adapter?.isEnabled == true)
            startScan()
        ticker.postDelayed({ tick(n + 1) }, TICK_MS / timeScale)
    }

    /** the address of a LEAKCAM found nearby now, by its device ID; null when it is not */
    private fun addressOf(id: String) = if (id.isNotEmpty() && id == usbId) USB_TAG
        else ids.entries.firstOrNull { it.value == id && found.containsKey(it.key) }?.key

    /** the status list: the remembered LEAKCAMs (presence, network, Delete: in range or not) and new ones found
     *  nearby, one radio each; the setup button acts on the picked one and is off while it is out of BLE range */
    private fun render() {
        val k = known()
        if (usbId.isNotEmpty())
            selected = usbId                 // the board on the cable is always the one picked (user 2026-11-05)
        val all = rows.filter { k.has(it.first) }.toMutableList()
        for ((address, d) in found)
            ids[address]?.takeIf { !k.has(it) }?.let { all.add(Triple(it, d.second, getString(R.string.mine_new))) }
        if (usbId.isNotEmpty() && all.none { it.first == usbId })
            all.add(Triple(usbId, usbId, getString(R.string.mine_new)))
        if (all.none { it.first == selected })
            selected = (all.firstOrNull { addressOf(it.first) != null } ?: all.firstOrNull())?.first.orEmpty()
        // every scan result and presence refresh lands here: the views are rebuilt only for a change, a rebuild under
        // a finger lost the tap (test111's Delete, 2026-11-01)
        val now = all.joinToString("|") {
            val a = addressOf(it.first)
            "${it.first},${it.third},${a != null},${online[it.first]},${a?.let { x -> usbPower[x] }},${a?.let { x -> barCount(rssi[x] ?: -100) }},${a?.let { x -> stored[x] }}"
        } +
            "|$selected|${k.length()}|${wakeWaiting(selected)}|$scannedOnStatus"
        if (now == shown)
            return
        shown = now
        mineList.removeAllViews()
        for ((id, _, line) in all)
            mineList.addView(LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = android.view.Gravity.CENTER_VERTICAL    // the name, the icons and the menu on one line
                val reachable = addressOf(id) != null
                val up = online[id] == true
                val red = k.has(id) && !up      // one of mine with no presence (offline, never seen, no server)
                addView(RadioButton(context).apply {
                    // its serial (the device ID, as on the USB serial number and the server) and its network (user:
                    // minimum text, no shared "LEAKCAM-" prefix); the state is in the icons, no presence in red
                    text = if (k.has(id)) deviceText(id) else "$id · $line"
                    if (red)
                        setTextColor(0xFFC62828.toInt())
                    isChecked = id == selected
                    contentDescription = "pick $id"
                    setOnClickListener { selected = id; render() }
                }, LinearLayout.LayoutParams(-2, -2))     // the icons right after the name, not at the far end
                val address = addressOf(id)
                val chg = onUsbPower(id, address)
                val pct = batteryMv[id]?.let { BatteryIcon.percent(it) }
                Log.i(TAG, "row $id wifi ${if (k.has(id) && up) 1 else 0} ble ${if (reachable) 1 else 0} red ${if (red) 1 else 0}" +
                      " power ${if (address == null) "-" else if (address == USB_TAG) "usb" else if (usbPower[address] == true) "plug" else "battery"}" +
                      " bars ${address?.takeIf { it != USB_TAG }?.let { barCount(rssi[it] ?: -100) } ?: 0}" +
                      " rssi ${address?.takeIf { it != USB_TAG }?.let { rssi[it] } ?: 0}" +
                      " bat ${pct ?: -1} chg ${if (chg) 1 else 0}")   // test118, test113, test131
                val gap = (6 * resources.displayMetrics.density).toInt()   // the icons a little apart from the name
                for (icon in rowIcons(id, address))
                    addView(android.widget.ImageView(context).apply {
                        setPaddingRelative(gap, 0, 0, 0)
                        setImageDrawable(icon.drawable)
                        contentDescription = icon.what
                        tooltipText = icon.tip
                        if (address != null && icon.what == getString(R.string.icon_signal))
                            setOnLongClickListener { showDbm(address); true }
                    })
            })
        val name = all.firstOrNull { it.first == selected }?.second.orEmpty()
        val here = addressOf(selected) != null  // its Wi-Fi can be set only in BLE range
        // one of mine present at the server but out of range: woken through the server (its next check-in opens BLE
        // for a minute, tests/test119); without presence it advertises by itself when its Wi-Fi is gone (test117)
        // only once a scan missed it: right after opening, a board in range was offered Wake for a moment (test117)
        val wakeable = !here && scannedOnStatus && k.has(selected) && online[selected] == true
        val waiting = wakeWaiting(selected)
        nearby.text = when {
            note?.let { it.first == selected && SystemClock.elapsedRealtime() < it.third } == true -> note?.second
            name.isEmpty() -> getString(R.string.nearby_none)
            addressOf(selected) == USB_TAG -> getString(R.string.nearby_usb, name)
            here && k.has(selected) -> getString(R.string.nearby_known, name)
            here -> getString(R.string.nearby_new, name)
            waiting -> getString(R.string.nearby_waking, name, waking[selected]?.second ?: 60)
            wakeable -> getString(R.string.nearby_wake, name)
            k.has(selected) -> getString(R.string.nearby_lost, name)
            else -> getString(R.string.nearby_away, name)
        }
        // one of mine: Take photo / Take video, the rest in the menu; else Set up in the same place (one row height:
        // nothing jumps). The labels are the action alone: the board is the one picked in the list above
        val isMine = k.has(selected)
        setupOpen.visibility = if (isMine) View.GONE else View.VISIBLE
        takePhoto.visibility = if (isMine) View.VISIBLE else View.GONE
        takeVideo.visibility = if (isMine) View.VISIBLE else View.GONE
        setupOpen.text = getString(if (name.isEmpty()) R.string.setup_new else R.string.setup_new_named)
        setupOpen.isEnabled = here
        changeLabel = getString(if (wakeable) R.string.setup_wake else R.string.setup_change)
        changeEnabled = here || (wakeable && !waiting)
        // Delete only for a board set up (user 2026-11-05): known by its own word, the cable's ID answer or the
        // advertisement's flag; nothing to remove from a board not set up
        deletable = isMine && addressOf(selected)?.let { stored[it] } != false
        more.visibility = if (isMine) View.VISIBLE else View.INVISIBLE   // keeps its place: the buttons never change width
        // what its menu offers now, for screen readers (and the test driver: a disabled popup item still reads as
        // enabled to UI Automator, 2026-11-07)
        more.contentDescription = getString(R.string.more) + ": " + changeLabel + if (changeEnabled) "" else " " + getString(R.string.not_now)
        Log.i(TAG, "button ${if (isMine) changeLabel else setupOpen.text} enabled ${if (if (isMine) changeEnabled else here) 1 else 0}")   // test119
    }

    /** a wake asked for this board and not yet answered: its check-in interval plus the 2 minutes it advertises */
    private fun wakeWaiting(id: String) = waking[id]?.let { (t, within) ->
        SystemClock.elapsedRealtime() - t < (within + 120) * 1000L && addressOf(id) == null } ?: false

    /** ask the server to wake a board out of range (POST /v1/devices/{id}/wake): it advertises after its next check-in;
     *  the status screen scans every 10 s meanwhile and the setup button turns into "Change the Wi-Fi" when it shows */
    private fun wake(id: String) {
        if (id.isEmpty())
            return
        setupOpen.isEnabled = false
        Thread {
            val (code, within) = try {
                val c = URL("${BuildConfig.SERVER}/v1/devices/$id/wake").openConnection() as HttpURLConnection
                c.requestMethod = "POST"
                c.connectTimeout = 3000
                c.readTimeout = 3000
                val code = c.responseCode
                val within = if (code == 200) JSONObject(c.inputStream.bufferedReader().use { it.readText() })
                    .optInt("within_s", 60) else 60
                c.disconnect()
                code to within
            } catch (e: Exception) {
                -1 to 0
            }
            runOnUiThread {
                Log.i(TAG, "wake $id answer $code")
                if (code == 200 || code == 429)  // 429: woken less than 2 minutes ago, it may still advertise
                    waking[id] = SystemClock.elapsedRealtime() to within
                else
                    nearby.text = getString(R.string.err_wake, id)
                shown = ""
                render()
                if ((code == 200 || code == 429) && !scanning && permissionsGranted() && adapter?.isEnabled == true)
                    startScan()              // at once; the ticker scans every 10 s meanwhile
            }
        }.start()
    }

    /** setup: the full four steps (address null: Find first), or a known LEAKCAM's Wi-Fi only */
    private fun openSetup(address: String?) {
        if (address == USB_TAG || (address == null && usbId.isNotEmpty()))
            return openUsb()                // a board on the cable: its own screen, over USB
        onStatus = false
        usbFlow = false
        usbHeader.visibility = View.GONE
        devices.visibility = View.VISIBLE
        sendButton.setText(R.string.send)
        devices.findViewWithTag<View>(USB_TAG)?.let { devices.removeView(it) }
        ticker.removeCallbacksAndMessages(null)
        if (address != null)
            stopScan()                  // the board is found; Send stops it anyway
        statusPanel.visibility = View.GONE
        setupPanel.visibility = View.VISIBLE
        backOffered = known().length() > 0
        setupMenu()
        changeOnly = address != null && ids[address]?.let { known().has(it) } == true
        val hidden = address != null && stored[address] == true
        for (v in listOf<View>(findViewById(R.id.intro), steps[0], steps[1], steps[2]))
            v.visibility = if (changeOnly) View.GONE else View.VISIBLE
        val current = address?.let { ids[it] }?.let { ssids[it] }.orEmpty()
        ssidField.setText(current)              // the network it is on now (not secret); the passphrase stays hidden
        passwordField.text.clear()
        ssidField.hint = getString(if (hidden && current.isEmpty()) R.string.hidden_ssid else R.string.ssid)
        passwordField.hint = getString(if (hidden) R.string.hidden_password else R.string.password)
        if (address == null) {
            show(STEP_PLUG, R.string.help_plug)
            // no tap for Find when nothing has to be asked first: the full Find took 10 s of the setup (test106). A
            // running scan goes on: Android ignores an app that starts more than 5 scans in 30 s (test111)
            // no Find button: the list fills by itself (user 2026-11-07); ready() asks for what is missing first
            if (scanning)
                show(STEP_FIND, R.string.help_scanning)
            else if (ready())
                startScan()
        } else {
            // the board the status screen found, picked: Send needs no second Find
            if (address == USB_TAG)
                addUsbRadio()
            for (i in 0 until devices.childCount)
                if (devices.getChildAt(i).tag == address)
                    devices.check(devices.getChildAt(i).id)
            sendButton.isEnabled = true
            wifiHelp()
        }
    }

    // ---------------- steps 3 and 4: Wi-Fi, pair and save ----------------

    private fun send(check: Boolean) {
        val address = pickedTag()
        val ssid = ssidField.text.toString().toByteArray(Charsets.UTF_8)
        val psk = passwordField.text.toString().toByteArray(Charsets.UTF_8)
        if (address == null || (address != USB_TAG && !found.containsKey(address)))
            return problem(STEP_FIND, R.string.err_pick)
        if (ssid.isEmpty() || ssid.size > 32)
            return problem(STEP_WIFI, R.string.err_ssid)
        if (psk.isNotEmpty() && psk.size !in 8..63)
            return problem(STEP_WIFI, R.string.err_password)
        if (address == USB_TAG)
            return sendUsb(check, ssid, psk)
        val (device, name) = found.getValue(address)
        stopScan()
        /* the board keeps no pairings (DESIGN.md 5.3): a bond from an earlier setup is stale, every send pairs anew */
        if (device.bondState != BluetoothDevice.BOND_NONE && !removeBond(device))
            return problem(STEP_SAVE, R.string.err_unpair, name)
        boardName = name
        boardId = ids[address] ?: ""
        ssidText = ssidField.text.toString()
        checked = check
        committed = false
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
        reconciling = false
        viaUsb = false
        usbAwait = null
        usbSteps.clear()
        usbLater.clear()
        usbSwitching = false
        sendButton.isEnabled = true
        if (wasSending)
            done?.invoke()
    }

    private fun fail(res: Int, vararg args: Any) = finishSend { problem(STEP_SAVE, res, *args) }

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
        }
        firstRead = true
        writes = ArrayDeque()
        if (adapter?.isEnabled != true)
            return show(STEP_SAVE, R.string.help_bluetooth_off_reconnect, boardName)
        show(STEP_SAVE, R.string.help_reconnecting, boardName)
        main.postDelayed({ if (sending && gatt == null) connect() }, RETRY_MS)
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
            WCHK_SAVED -> finishSend {
                remember()
                show(STEP_DONE, if (checked) R.string.help_done else R.string.help_done_unchecked, boardName, ssidText)
                alertPermissions()
            }
            else -> {
                // where the board is now may be the only problem: offer to store unchecked
                val placeOnly = state in setOf(WCHK_NOT_FOUND, WCHK_NO_ADDRESS, WCHK_NO_DNS, WCHK_NO_INTERNET, WCHK_CAPTIVE, WCHK_NO_API)
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
    private fun openUsb() {
        onStatus = false
        usbFlow = true
        ticker.removeCallbacksAndMessages(null)
        stopScan()
        statusPanel.visibility = View.GONE
        setupPanel.visibility = View.VISIBLE
        for (v in listOf<View>(findViewById(R.id.intro), steps[0], steps[1], steps[2], steps[3], devices))
            v.visibility = View.GONE
        usbHeader.visibility = View.VISIBLE
        usbHeader.text = getString(R.string.usb_header, usbId)
        backOffered = known().length() > 0
        setupMenu()
        changeOnly = known().has(usbId)
        addUsbRadio()                        // hidden: send() takes the picked one
        val current = ssids[usbId].orEmpty()
        ssidField.setText(current)
        passwordField.text.clear()
        ssidField.hint = getString(if (usbStored && current.isEmpty()) R.string.hidden_ssid else R.string.ssid)
        passwordField.hint = getString(if (usbStored) R.string.hidden_password else R.string.password)
        sendButton.setText(R.string.usb_send)
        sendButton.isEnabled = true
        anywayOffered = false
        setupMenu()
        if (usbStored)
            show(STEP_WIFI, R.string.help_usb_stored, usbId, if (current.isEmpty()) "" else " (\"$current\")")
        else {
            show(STEP_WIFI, R.string.help_usb_wifi, usbId)
            ssidField.requestFocus()         // no network: the fields are the screen
        }
        Log.i(TAG, "usb screen $usbId creds ${if (usbStored) 1 else 0}")   // test113
    }

    /** the board on the cable as the one picked in the setup list (once) */
    private fun addUsbRadio() {
        if (devices.findViewWithTag<View>(USB_TAG) == null)
            devices.addView(RadioButton(this).apply {
                id = View.generateViewId()
                tag = USB_TAG
                text = deviceText(usbId)
                setCompoundDrawablesRelative(null, null, iconStrip(usbId, USB_TAG), null)
            }, 0)
        devices.check(devices.findViewWithTag<View>(USB_TAG).id)
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
        val leave = usbFlow && !sending && step != STEP_DONE   // Done stays: the board ends the link itself after it
        Log.i(TAG, "usb board ${usbId.ifEmpty { "-" }} gone")
        usbId = ""
        ids.remove(USB_TAG)
        stored.remove(USB_TAG)
        devices.findViewWithTag<View>(USB_TAG)?.let { devices.removeView(it) }
        if (onStatus) {
            shown = ""
            render()
        }
        if (leave) {
            if (known().length() > 0) showStatus() else openSetup(null)
            if (!onStatus)
                show(STEP_PLUG, R.string.help_usb_unplugged)
        }
        startDecide()
    }

    /** the K230's relay starts with its program (~0.6 s after its boot), maybe after the USB device: ask until answered */
    private fun askId(n: Int) {
        usbIdTries = n
        if (usbId.isNotEmpty() || !usbLink.up || n >= USB_ID_TRIES)
            return startDecide()             // answered, gone, or never answering: the start goes on
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
                // the start decides at DECIDE_MS; later a plug just adds the row, picked (the screen adjusted on the
                // fly), never over a setup that runs or its Done (the board just set up back on its relay)
                if (!starting && onStatus) {
                    selected = usbId
                    render()
                }
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
        boardId = usbId
        ssidText = ssidField.text.toString()
        checked = check
        committed = false
        attempt = (1..Int.MAX_VALUE).random()
        viaUsb = true
        sending = true
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
     *  network; then done(). No server: the list unchanged, said so per row */
    private fun loadMine(done: () -> Unit = {}) {
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
                    }
                }
                rows = (list?.map { j -> Triple(j.getString("id"), j.getString("id"), line(j)) }
                        ?: mine.keys().asSequence().map { Triple(it, it, getString(R.string.mine_unknown, it)) }.toList())
                for (r in rows)
                    Log.i(TAG, "mine ${r.first}: ${r.third}")
                if (onStatus)
                    render()
                done()
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
        return getString(R.string.mine_ssid, state, ssid.ifEmpty { getString(R.string.mine_ssid_unknown) })
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
                        loadMine { if (known().length() == 0) openSetup(null) }
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
        backOffered = true
        setupMenu()
    }


    /** the list and each board's presence, from the server; logged for tests/test111 */
    private fun refreshPresence() = loadMine()

    private fun what(uuid: UUID) = getString(when (uuid) {
        SSID -> R.string.what_ssid
        PSK -> R.string.what_psk
        else -> R.string.what_commit
    })
}
