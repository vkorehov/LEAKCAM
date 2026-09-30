package com.leakcam.setup

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
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
import android.text.InputType
import android.view.View
import android.view.WindowInsets
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import java.util.UUID

/**
 * Sets the Wi-Fi network of a LEAKCAM over BLE (firmware/bl616/ble_pairing.h).
 *
 * The board advertises the provisioning service while it is on USB power. The app scans for that
 * service, connects, pairs (LE Secure Connections, Just Works: the board accepts pairing only on USB
 * power), subscribes to STATUS, then writes SSID, passphrase and commit. Commit 0x01 makes the
 * board join the network, get an address and reach the internet before it stores anything; STATUS
 * reports each step and the result (firmware/bl616/wifi_check_calc.h). Commit 0x02 stores without
 * checking, for a board set up out of reach of its network.
 *
 * The screen walks through four steps (plug in, find, Wi-Fi, pair and save); the box under them
 * says what to do now, and for pairing what the phone will show.
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
        const val MTU = 247              // the longest value, a 63-byte passphrase, in one write
        const val SCAN_MS = 10_000L
        const val SEND_TIMEOUT_MS = 60_000L   // per step: pairing (the user reads and accepts), each check step
        const val REQUEST_PERMISSIONS = 1
        const val REQUEST_ENABLE_BT = 2
        const val STEP_PLUG = 1
        const val STEP_FIND = 2
        const val STEP_WIFI = 3
        const val STEP_SAVE = 4
        const val STEP_DONE = 5
        val PAIRING_LOST = setOf(5, 6, 0x3D)  // HCI: authentication failure, key missing, MIC failure
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

    private val main = Handler(Looper.getMainLooper())
    private val adapter: BluetoothAdapter? by lazy {
        (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }
    private val found = LinkedHashMap<String, Pair<BluetoothDevice, String>>()   // address -> device, name
    private var scanning = false
    private var step = STEP_PLUG

    // one send at a time
    private var gatt: BluetoothGatt? = null
    private var boardName = ""
    private var wasBonded = false        // paired before this send: a pairing failure means a stale bond
    private var checked = true           // this send asks the board to check the network first
    private var committed = false        // commit written: STATUS reports from here on
    private var ssidText = ""
    private var writes = ArrayDeque<Pair<UUID, ByteArray>>()
    private var bondReceiver: BroadcastReceiver? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        steps = listOf(R.id.step1, R.id.step2, R.id.step3, R.id.step4).map { findViewById(it) }
        stepTexts = steps.map { it.text.toString() }
        help = findViewById(R.id.help)
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
        show(STEP_PLUG, R.string.help_plug)
    }

    override fun onDestroy() {
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
        main.postDelayed({
            stopScan()
            when {
                found.isEmpty() -> problem(STEP_PLUG,
                    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) R.string.help_none_found_location
                    else R.string.help_none_found)
                found.size == 1 -> show(STEP_WIFI, R.string.help_wifi)
                else -> show(STEP_WIFI, R.string.help_pick)
            }
        }, SCAN_MS)
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
        found[d.address] = d to name
        devices.addView(RadioButton(this).apply {
            id = View.generateViewId()
            tag = d.address
            text = getString(R.string.device_entry, name, r.rssi)
        })
        if (found.size == 1)
            devices.check(devices.getChildAt(0).id)
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
        boardName = name
        ssidText = ssidField.text.toString()
        wasBonded = device.bondState == BluetoothDevice.BOND_BONDED
        checked = check
        committed = false
        val commit = if (check) COMMIT_CHECK else COMMIT_UNCHECKED
        writes = ArrayDeque(listOf(SSID to ssid, PSK to psk, COMMIT to byteArrayOf(commit)))
        sendButton.isEnabled = false
        scanButton.isEnabled = false
        saveAnywayButton.visibility = View.GONE
        show(STEP_SAVE, R.string.help_connecting, name)
        armTimeout()
        gatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private fun armTimeout() {
        main.removeCallbacksAndMessages(null)
        main.postDelayed({ fail(R.string.err_timeout) }, SEND_TIMEOUT_MS)
    }

    /** closes the connection and turns the buttons back on; then [done], if a send was running */
    private fun finishSend(done: (() -> Unit)?) {
        main.removeCallbacksAndMessages(null)
        bondReceiver?.let { unregisterReceiver(it) }
        bondReceiver = null
        gatt?.let {
            if (permissionsGranted()) {
                it.disconnect()
                it.close()
            }
        }
        val wasSending = gatt != null
        gatt = null
        sendButton.isEnabled = true
        scanButton.isEnabled = true
        if (wasSending)
            done?.invoke()
    }

    private fun fail(res: Int, vararg args: Any) = finishSend { problem(STEP_SAVE, res, *args) }

    /** a pairing that did not complete: declined or on battery, or a bond the board no longer has */
    private fun pairingFailed() =
        if (wasBonded) fail(R.string.err_pairing_stale, boardName) else fail(R.string.err_pairing_refused)

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, st: Int, newState: Int) {
            main.post {
                if (g != gatt) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    if (!g.requestMtu(MTU))
                        pairThenDiscover(g)
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    when {
                        bondReceiver != null -> pairingFailed()                  // dropped while pairing
                        wasBonded && st in PAIRING_LOST -> pairingFailed()
                        committed -> fail(R.string.err_lost_during_check)
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
                if (g.getService(SERVICE) == null) fail(R.string.err_no_service) else subscribe(g)
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

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, st: Int) {
            main.post {
                if (g != gatt) return@post
                when (st) {
                    BluetoothGatt.GATT_SUCCESS -> writeNext(g)
                    BluetoothGatt.GATT_INSUFFICIENT_AUTHENTICATION, BluetoothGatt.GATT_INSUFFICIENT_ENCRYPTION -> pairingFailed()
                    else -> fail(R.string.err_refused, what(c.uuid), st)
                }
            }
        }
    }

    /** the board asks for encryption on connect; wait for the bond before touching the service */
    private fun pairThenDiscover(g: BluetoothGatt) {
        val device = g.device
        if (device.bondState == BluetoothDevice.BOND_BONDED) {
            show(STEP_SAVE, R.string.help_already_paired, boardName)
            g.discoverServices()
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
                        show(STEP_SAVE, R.string.help_sending)
                        g.discoverServices()
                    }
                    BluetoothDevice.BOND_NONE -> pairingFailed()
                }
            }
        }
        bondReceiver = receiver
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)   // system broadcasts still arrive
        else
            registerReceiver(receiver, filter)
        if (device.bondState == BluetoothDevice.BOND_NONE)
            device.createBond()
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

    /** the board's check: progress, then SAVED or a failure (nothing stored) */
    private fun onStatus(v: ByteArray) {
        if (!committed || v.isEmpty()) return
        val state = v[0].toInt() and 0xff
        val detail = if (v.size >= 3) (v[1].toInt() and 0xff) or ((v[2].toInt() and 0xff) shl 8) else 0
        armTimeout()
        when (state) {
            WCHK_JOINING -> show(STEP_SAVE, R.string.help_check_join, ssidText)
            WCHK_ADDRESS -> show(STEP_SAVE, R.string.help_check_address)
            WCHK_INTERNET -> show(STEP_SAVE, R.string.help_check_internet)
            WCHK_SAVED -> finishSend {
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
        if (next == null)
            return                       // committed: the board reports on STATUS

        val (uuid, value) = next
        if (uuid == COMMIT)
            committed = true             // STATUS may answer before the write does (save, busy)
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

    private fun what(uuid: UUID) = getString(when (uuid) {
        SSID -> R.string.what_ssid
        PSK -> R.string.what_psk
        else -> R.string.what_commit
    })
}
