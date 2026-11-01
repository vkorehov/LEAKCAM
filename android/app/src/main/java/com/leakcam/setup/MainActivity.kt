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
 * Sets the Wi-Fi network of a LEAKCAM over BLE (firmware/bl616/ble_pairing.h).
 *
 * The board advertises the provisioning service while it is on USB power. The app scans for that
 * service, connects, pairs (LE Secure Connections, Just Works: the board accepts pairing only on USB
 * power), subscribes to STATUS, then writes SSID, passphrase and commit. Commit 0x01 makes the
 * board join the network, get an address and reach the internet before it stores anything; STATUS
 * reports each step and the result (firmware/bl616/wifi_check_calc.h). STATUS is the board's state for the
 * commit's attempt id: read once a second as well, so a lost notification only delays the answer; after a lost
 * link (or Bluetooth off) the app reconnects and reads it, and never claims what the board did without it
 * (tests/test110). A failed connect is retried. Commit 0x02 stores without
 * checking, for a board set up out of reach of its network.
 *
 * The screen walks through four steps (plug in, find, Wi-Fi, pair and save); the box under them
 * says what to do now, and for pairing what the phone will show.
 *
 * More than one LEAKCAM (use case 2, tests/test111): the app opens on a status screen, the LEAKCAMs set up from this
 * phone online or offline as the presence server sees them (BuildConfig.SERVER, GET /v1/devices/<id>, every 10 s),
 * each deletable from the list. Setup opens only for a LEAKCAM found on USB power (it advertises only then) or when
 * the list is empty. Each LEAKCAM advertises its device ID and a flags byte (scan response, manufacturer data,
 * company 0xFFFF: the BL616's factory MAC, bit 0 a stored network): a known one gets the short "change its Wi-Fi"
 * flow. The app never stores Wi-Fi credentials: a stored network shows as hidden, typing replaces it.
 */
@SuppressLint("MissingPermission") // every Bluetooth call runs after permissionsGranted()
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
        // STATUS states, as in firmware/bl616/wifi_check_calc.h
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
        const val STEP_PLUG = 1
        const val STEP_FIND = 2
        const val STEP_WIFI = 3
        const val STEP_SAVE = 4
        const val STEP_DONE = 5
        const val UNPAIR_MS = 5_000L          // the phone removes an old bond
    }

    private lateinit var steps: List<TextView>
    private lateinit var stepTexts: List<String>
    private lateinit var help: TextView
    private lateinit var scanButton: Button
    private lateinit var sendButton: Button
    private lateinit var saveAnywayButton: Button
    private lateinit var devices: RadioGroup
    private lateinit var ssidField: EditText
    private lateinit var passwordField: EditText
    private lateinit var statusPanel: View
    private lateinit var setupPanel: View
    private lateinit var mineList: LinearLayout
    private lateinit var nearby: TextView
    private lateinit var setupOpen: Button
    private lateinit var toStatus: Button

    private val main = Handler(Looper.getMainLooper())
    private val adapter: BluetoothAdapter? by lazy {
        (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }
    private val found = LinkedHashMap<String, Pair<BluetoothDevice, String>>()   // address -> device, name
    private val ids = HashMap<String, String>()   // address -> device ID from the advertisement
    private val stored = HashMap<String, Boolean>()   // address -> the board has a Wi-Fi network (flags bit 0)
    private val ticker = Handler(Looper.getMainLooper())   // the status screen's refresh; main's callbacks get cleared
    private var onStatus = false
    private var changeOnly = false       // setup of a known LEAKCAM: only its Wi-Fi
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
    private var discovered = false       // services discovered on this connection
    private var bonded = false           // and encrypted: the writes can start

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        steps = listOf(R.id.step1, R.id.step2, R.id.step3, R.id.step4).map { findViewById(it) }
        stepTexts = steps.map { it.text.toString() }
        help = findViewById(R.id.help)
        statusPanel = findViewById(R.id.status_panel)
        setupPanel = findViewById(R.id.setup_panel)
        mineList = findViewById(R.id.mine_list)
        nearby = findViewById(R.id.nearby)
        setupOpen = findViewById(R.id.setup_open)
        toStatus = findViewById(R.id.to_status)
        scanButton = findViewById(R.id.scan)
        sendButton = findViewById(R.id.send)
        saveAnywayButton = findViewById(R.id.save_anyway)
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
        devices.setOnCheckedChangeListener { _, _ -> if (!scanning && gatt == null) show(STEP_WIFI, R.string.help_wifi) }
        scanButton.setOnClickListener { if (ready()) startScan() }
        sendButton.setOnClickListener { if (ready()) send(true) }
        saveAnywayButton.setOnClickListener { if (ready()) send(false) }
        setupOpen.setOnClickListener { found.keys.firstOrNull()?.let { openSetup(it) } }
        toStatus.setOnClickListener { if (!sending) showStatus() }
        if (known().length() == 0) openSetup(null) else showStatus()
    }

    override fun onDestroy() {
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

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
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
        found.clear()
        devices.removeAllViews()
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        scanning = true
        scanButton.isEnabled = false
        sendButton.isEnabled = false
        show(STEP_FIND, R.string.help_scanning)
        main.postDelayed({ scanDone() }, SCAN_MS)
    }

    private fun scanDone() {
        stopScan()
        if (onStatus) {
            if (found.isEmpty()) {
                nearby.setText(R.string.nearby_none)
                setupOpen.setText(R.string.setup_new)
                setupOpen.isEnabled = false
            }
            return
        }
        if (found.isEmpty())
            problem(STEP_PLUG,
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) R.string.help_none_found_location
                else R.string.help_none_found)
    }

    private fun stopScan() {
        main.removeCallbacksAndMessages(null)
        if (scanning && permissionsGranted())
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        scanning = false
        scanButton.isEnabled = true
        sendButton.isEnabled = true
    }

    private fun addDevice(r: ScanResult) {
        val d = r.device
        if (found.containsKey(d.address))
            return
        val name = r.scanRecord?.deviceName ?: d.name ?: "LEAKCAM"
        val mfg = r.scanRecord?.getManufacturerSpecificData(0xFFFF)
        val devId = mfg?.takeIf { it.size >= 6 }?.copyOfRange(0, 6)?.joinToString("") { "%02X".format(it) }
        if (devId != null)
            ids[d.address] = devId
        stored[d.address] = mfg != null && mfg.size >= 7 && (mfg[6].toInt() and 1) != 0
        Log.i(TAG, "found $name id ${devId ?: "none"} creds ${if (stored[d.address] == true) 1 else 0}")   // test111
        found[d.address] = d to name
        devices.addView(RadioButton(this).apply {
            id = View.generateViewId()
            tag = d.address
            text = getString(if (devId != null && known().has(devId)) R.string.device_entry_known else R.string.device_entry,
                name, r.rssi)
        })
        // the first board leads on at once, Find goes on behind (another board nearby joins the list) until Send:
        // waiting for the whole Find took 10 s of the setup, then 1.7 s (test106)
        if (onStatus)
            return showNearby(name, devId)
        when (found.size) {
            1 -> {
                devices.check(devices.getChildAt(0).id)
                sendButton.isEnabled = true
                wifiHelp()
            }
            2 -> show(STEP_WIFI, R.string.help_pick)
        }
    }

    private fun wifiHelp() = if (changeOnly) show(STEP_WIFI, R.string.help_change, boardLabel()) else show(STEP_WIFI, R.string.help_wifi)

    private fun boardLabel() = found.values.firstOrNull()?.second ?: "LEAKCAM"

    // ---------------- the status screen ----------------

    /** the LEAKCAMs set up from this phone and their presence, refreshed every 10 s; a scan for one on USB power */
    private fun showStatus() {
        onStatus = true
        setupPanel.visibility = View.GONE
        statusPanel.visibility = View.VISIBLE
        nearby.setText(R.string.nearby_none)
        setupOpen.setText(R.string.setup_new)
        setupOpen.isEnabled = false
        ticker.removeCallbacksAndMessages(null)
        tick(0)
    }

    private fun tick(n: Int) {
        if (!onStatus)
            return
        refreshPresence()
        if (n % 3 == 0 && !scanning && permissionsGranted() && adapter?.isEnabled == true)
            startScan()                 // every 30 s: a LEAKCAM plugged in shows up, one unplugged goes
        ticker.postDelayed({ tick(n + 1) }, 10_000L)
    }

    private fun showNearby(name: String, devId: String?) {
        val isKnown = devId != null && known().has(devId)
        nearby.text = getString(if (isKnown) R.string.nearby_known else R.string.nearby_new, name)
        setupOpen.text = getString(if (isKnown) R.string.setup_change else R.string.setup_new_named, name)
        setupOpen.isEnabled = true
    }

    /** setup: the full four steps (address null: Find first), or a known LEAKCAM's Wi-Fi only */
    private fun openSetup(address: String?) {
        onStatus = false
        ticker.removeCallbacksAndMessages(null)
        stopScan()
        statusPanel.visibility = View.GONE
        setupPanel.visibility = View.VISIBLE
        toStatus.visibility = if (known().length() > 0) View.VISIBLE else View.GONE
        changeOnly = address != null && ids[address]?.let { known().has(it) } == true
        val hidden = address != null && stored[address] == true
        for (v in listOf<View>(findViewById(R.id.intro), steps[0], steps[1], steps[2]))
            v.visibility = if (changeOnly) View.GONE else View.VISIBLE
        ssidField.text.clear()
        passwordField.text.clear()
        ssidField.hint = getString(if (hidden) R.string.hidden_ssid else R.string.ssid)
        passwordField.hint = getString(if (hidden) R.string.hidden_password else R.string.password)
        if (address == null) {
            show(STEP_PLUG, R.string.help_plug)
            // no tap for Find when nothing has to be asked first: the full Find took 10 s of the setup (test106)
            if (permissionsGranted() && adapter?.isEnabled == true)
                startScan()
        } else {
            // the board the status screen found, picked: Send needs no second Find
            for (i in 0 until devices.childCount)
                if (devices.getChildAt(i).tag == address)
                    devices.check(devices.getChildAt(i).id)
            sendButton.isEnabled = true
            wifiHelp()
        }
    }

    // ---------------- steps 3 and 4: Wi-Fi, pair and save ----------------

    private fun send(check: Boolean) {
        val address = devices.findViewById<RadioButton>(devices.checkedRadioButtonId)?.tag as String?
        val (device, name) = address?.let { found[it] } ?: return problem(STEP_FIND, R.string.err_pick)
        val ssid = ssidField.text.toString().toByteArray(Charsets.UTF_8)
        val psk = passwordField.text.toString().toByteArray(Charsets.UTF_8)
        if (ssid.isEmpty() || ssid.size > 32)
            return problem(STEP_WIFI, R.string.err_ssid)
        if (psk.isNotEmpty() && psk.size !in 8..63)
            return problem(STEP_WIFI, R.string.err_password)
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
        scanButton.isEnabled = false
        saveAnywayButton.visibility = View.GONE
        show(STEP_SAVE, R.string.help_connecting, name)
        armTimeout()
        connectWhenUnpaired(device, SystemClock.elapsedRealtime() + UNPAIR_MS)
    }

    private fun removeBond(d: BluetoothDevice) = try {
        d.javaClass.getMethod("removeBond").invoke(d) as Boolean    // no public API; test040's BleOp uses it too
    } catch (e: ReflectiveOperationException) {
        false
    }

    private fun connectWhenUnpaired(device: BluetoothDevice, until: Long) {
        when {
            device.bondState == BluetoothDevice.BOND_NONE -> {
                connect()
                // the phone starts the pairing: Samsung then asks once, not twice as for the board's Security Request
                // (2.3 -> 1.2 s and one tap less, test106)
                device.createBond()
            }
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
        bondReceiver = null
        adapterReceiver?.let { unregisterReceiver(it) }
        adapterReceiver = null
        closeGatt()
        val wasSending = sending
        sending = false
        reconciling = false
        sendButton.isEnabled = true
        scanButton.isEnabled = true
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
            }
            else -> {
                // where the board is now may be the only problem: offer to store unchecked
                val placeOnly = state in setOf(WCHK_NOT_FOUND, WCHK_NO_ADDRESS, WCHK_NO_DNS, WCHK_NO_INTERNET, WCHK_CAPTIVE)
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
                        else -> problem(STEP_SAVE, R.string.chk_unknown, state)
                    }
                    saveAnywayButton.visibility = if (placeOnly) View.VISIBLE else View.GONE
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

    // ---------------- the LEAKCAMs set up from this phone ----------------

    private fun known(): JSONObject {
        val k = JSONObject(getSharedPreferences("devices", MODE_PRIVATE).getString("devices", null) ?: "{}")
        var old = false                  // an entry from before 2026-11-01 kept the SSID: dropped, the app stores none
        for (id in k.keys())
            old = k.getJSONObject(id).remove("ssid") != null || old
        if (old)
            save(k)
        return k
    }

    private fun save(k: JSONObject) =
        getSharedPreferences("devices", MODE_PRIVATE).edit().putString("devices", k.toString()).apply()

    private fun forget(id: String, name: String) {
        AlertDialog.Builder(this)
            .setMessage(getString(R.string.delete_ask, name))
            .setPositiveButton(R.string.delete) { _, _ ->
                save(known().apply { remove(id) })
                Log.i(TAG, "deleted $id")
                if (known().length() == 0) openSetup(null) else refreshPresence()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** the board just stored the network: kept with its name and network, then its presence asked */
    private fun remember() {
        if (boardId.isEmpty())
            return
        val k = known().put(boardId, JSONObject().put("name", boardName))   // never the network: the board has it
        save(k)
        toStatus.visibility = View.VISIBLE
    }

    /** one row per LEAKCAM: its presence line and Delete (content description "delete <id>" for the test driver) */
    private fun showMine(rows: List<Triple<String, String, String>>) {
        mineList.removeAllViews()
        for ((id, name, line) in rows)
            mineList.addView(LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                addView(TextView(context).apply { text = line }, LinearLayout.LayoutParams(0, -2, 1f))
                addView(Button(context).apply {
                    setText(R.string.delete)
                    contentDescription = "delete $id"
                    setOnClickListener { forget(id, name) }
                })
            })
    }

    /** each remembered LEAKCAM's presence from the server, off the main thread; logged for tests/test111 */
    private fun refreshPresence() {
        val k = known()
        val list = k.keys().asSequence().toList()
        if (mineList.childCount != list.size)
            showMine(list.map { Triple(it, k.getJSONObject(it).optString("name", it), k.getJSONObject(it).optString("name", it)) })
        if (list.isEmpty())
            return
        Thread {
            val rows = list.map { id ->
                val name = k.getJSONObject(id).optString("name", id)
                val line = try {
                    val c = URL("${BuildConfig.SERVER}/v1/devices/$id").openConnection() as HttpURLConnection
                    c.connectTimeout = 3000
                    c.readTimeout = 3000
                    val j = JSONObject(c.inputStream.bufferedReader().use { it.readText() })
                    c.disconnect()
                    val seen = j.getLong("last_seen_s")
                    when {
                        j.getBoolean("online") -> getString(R.string.mine_online, name)
                        seen < 0 -> getString(R.string.mine_never, name)
                        seen < 60 -> getString(R.string.mine_offline_s, name, seen.toInt())
                        else -> getString(R.string.mine_offline, name, (seen / 60).toInt())
                    }
                } catch (e: Exception) {          // no server, no network, a broken answer: said so, never a guess
                    getString(R.string.mine_unknown, name)
                }
                Log.i(TAG, "mine $id: $line")
                Triple(id, name, line)
            }
            runOnUiThread { if (onStatus) showMine(rows) }
        }.start()
    }

    private fun what(uuid: UUID) = getString(when (uuid) {
        SSID -> R.string.what_ssid
        PSK -> R.string.what_psk
        else -> R.string.what_commit
    })
}
