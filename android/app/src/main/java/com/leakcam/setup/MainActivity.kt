package com.leakcam.setup

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
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
 * power), then writes SSID, passphrase and commit (0x01), which stores both in the board's flash.
 * The board does not report whether the network works; it joins at its next wake.
 */
@SuppressLint("MissingPermission") // every Bluetooth call runs after permissionsGranted()
class MainActivity : Activity() {

    private companion object {
        val SERVICE: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000001")
        val SSID: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000002")
        val PSK: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000003")
        val COMMIT: UUID = UUID.fromString("4c43a000-4c45-4b43-414d-000000000004")
        const val MTU = 247              // the longest value, a 63-byte passphrase, in one write
        const val SCAN_MS = 10_000L
        const val SEND_TIMEOUT_MS = 45_000L   // includes the user accepting the pairing dialog
        const val REQUEST_PERMISSIONS = 1
        const val REQUEST_ENABLE_BT = 2
    }

    private lateinit var status: TextView
    private lateinit var scanButton: Button
    private lateinit var sendButton: Button
    private lateinit var devices: RadioGroup
    private lateinit var ssidField: EditText
    private lateinit var passwordField: EditText

    private val main = Handler(Looper.getMainLooper())
    private val adapter: BluetoothAdapter? by lazy {
        (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }
    private val found = LinkedHashMap<String, BluetoothDevice>()   // address -> device
    private var scanning = false

    // one send at a time
    private var gatt: BluetoothGatt? = null
    private var writes = ArrayDeque<Pair<UUID, ByteArray>>()
    private var bondReceiver: BroadcastReceiver? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        status = findViewById(R.id.status)
        scanButton = findViewById(R.id.scan)
        sendButton = findViewById(R.id.send)
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
        scanButton.setOnClickListener { if (ready()) startScan() }
        sendButton.setOnClickListener { if (ready()) send() }
    }

    override fun onDestroy() {
        stopScan()
        finishSend(null)
        super.onDestroy()
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
            say("This phone has no Bluetooth.")
            return false
        }
        if (!permissionsGranted()) {
            requestPermissions(neededPermissions(), REQUEST_PERMISSIONS)
            return false
        }
        if (!a.isEnabled) {
            @Suppress("DEPRECATION")
            startActivityForResult(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQUEST_ENABLE_BT)
            return false
        }
        return true
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        if (requestCode == REQUEST_PERMISSIONS)
            say(if (permissionsGranted()) "Bluetooth allowed, tap again." else "Bluetooth permission is needed to find the LEAKCAM.")
    }

    // ---------------- scan ----------------

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            main.post { addDevice(result) }
        }

        override fun onScanFailed(errorCode: Int) {
            main.post {
                scanning = false
                scanButton.isEnabled = true
                say("Scan failed (error $errorCode). Turn Bluetooth off and on, then try again.")
            }
        }
    }

    private fun startScan() {
        val scanner = adapter?.bluetoothLeScanner ?: return say("Bluetooth is not ready.")
        stopScan()
        found.clear()
        devices.removeAllViews()
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        scanning = true
        scanButton.isEnabled = false
        say("Scanning...")
        main.postDelayed({
            stopScan()
            say(if (found.isEmpty())
                "No LEAKCAM found. It advertises only while plugged into USB power." +
                    (if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) " Location must be on for scanning." else "")
            else "Pick the LEAKCAM, enter the Wi-Fi network, then save.")
        }, SCAN_MS)
    }

    private fun stopScan() {
        main.removeCallbacksAndMessages(null)
        if (scanning && permissionsGranted())
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        scanning = false
        scanButton.isEnabled = true
    }

    private fun addDevice(r: ScanResult) {
        val d = r.device
        if (found.containsKey(d.address))
            return
        found[d.address] = d
        val name = r.scanRecord?.deviceName ?: d.name ?: "LEAKCAM"
        devices.addView(RadioButton(this).apply {
            id = View.generateViewId()
            tag = d.address
            text = getString(R.string.device_entry, name, r.rssi)
        })
        if (found.size == 1)
            devices.check(devices.getChildAt(0).id)
    }

    // ---------------- send ----------------

    private fun send() {
        val address = devices.findViewById<RadioButton>(devices.checkedRadioButtonId)?.tag as String?
        val device = address?.let { found[it] } ?: return say("Scan and pick a LEAKCAM first.")
        val ssid = ssidField.text.toString().toByteArray(Charsets.UTF_8)
        val psk = passwordField.text.toString().toByteArray(Charsets.UTF_8)
        if (ssid.isEmpty() || ssid.size > 32)
            return say("The Wi-Fi name must be 1 to 32 bytes.")
        if (psk.isNotEmpty() && psk.size !in 8..63)
            return say("The Wi-Fi password must be 8 to 63 characters (empty for an open network).")
        stopScan()
        writes = ArrayDeque(listOf(SSID to ssid, PSK to psk, COMMIT to byteArrayOf(1)))
        sendButton.isEnabled = false
        scanButton.isEnabled = false
        say("Connecting to the LEAKCAM...")
        main.postDelayed({ finishSend("No answer from the LEAKCAM. Is it still on USB power?") }, SEND_TIMEOUT_MS)
        gatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    /** ends the send: message null = saved, otherwise the error to show */
    private fun finishSend(error: String?) {
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
        if (wasSending && error != null)
            say(error)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, st: Int, newState: Int) {
            main.post {
                if (g != gatt) return@post
                when {
                    newState == BluetoothProfile.STATE_CONNECTED -> {
                        say("Connected, preparing...")
                        if (!g.requestMtu(MTU))
                            pairThenDiscover(g)
                    }
                    newState == BluetoothProfile.STATE_DISCONNECTED ->
                        finishSend("The LEAKCAM disconnected (status $st). Keep it on USB power and try again.")
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, st: Int) {
            main.post { if (g == gatt) pairThenDiscover(g) }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, st: Int) {
            main.post {
                if (g != gatt) return@post
                if (g.getService(SERVICE) == null)
                    finishSend("This device has no LEAKCAM setup service.")
                else
                    writeNext(g)
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, st: Int) {
            main.post {
                if (g != gatt) return@post
                when (st) {
                    BluetoothGatt.GATT_SUCCESS -> writeNext(g)
                    BluetoothGatt.GATT_INSUFFICIENT_AUTHENTICATION, BluetoothGatt.GATT_INSUFFICIENT_ENCRYPTION ->
                        finishSend("Pairing did not complete. The LEAKCAM pairs only while on USB power. " +
                            "If it was set up before, remove it from the phone's Bluetooth devices and try again.")
                    else -> finishSend("The LEAKCAM refused the ${what(c.uuid)} (error $st).")
                }
            }
        }
    }

    /** the board asks for encryption on connect; wait for the bond before touching the service */
    private fun pairThenDiscover(g: BluetoothGatt) {
        val device = g.device
        if (device.bondState == BluetoothDevice.BOND_BONDED) {
            g.discoverServices()
            return
        }
        say("Pairing: accept the pairing request on the phone.")
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                @Suppress("DEPRECATION")
                val d = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                if (d?.address != device.address || g != gatt) return
                when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)) {
                    BluetoothDevice.BOND_BONDED -> {
                        say("Paired, sending...")
                        g.discoverServices()
                    }
                    BluetoothDevice.BOND_NONE ->
                        finishSend("Pairing refused. Plug the LEAKCAM into USB power and try again.")
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

    private fun writeNext(g: BluetoothGatt) {
        val next = writes.removeFirstOrNull()
        if (next == null) {
            val name = ssidField.text.toString()
            finishSend(null)
            say("Saved. The LEAKCAM will use \"$name\" from its next wake.")
            return
        }
        val (uuid, value) = next
        val c = g.getService(SERVICE)?.getCharacteristic(uuid)
            ?: return finishSend("This LEAKCAM has no ${what(uuid)} setting.")
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
            finishSend("Could not send the ${what(uuid)}.")
    }

    private fun what(uuid: UUID) = when (uuid) {
        SSID -> "Wi-Fi name"
        PSK -> "Wi-Fi password"
        else -> "save command"
    }

    private fun say(text: String) {
        status.text = text
    }
}
