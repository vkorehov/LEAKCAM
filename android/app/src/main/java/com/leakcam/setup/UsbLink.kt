package com.leakcam.setup

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * The USB cable to a LEAKCAM (DESIGN.md 5.3.4, 5.3.8; tests/test113): the phone is the USB host, the board's K230 a CDC
 * ACM device (1209:ABD1) while its setup firmware runs the USB setup relay (k230_app/leakcam.c). Lines both ways, one
 * per command (firmware/bl616_setup/usb_setup.c); this class only carries them: open, read lines, write lines, up/down.
 * Android's own USB host API, no library. The app is the device's default handler (res/xml/usb_device_filter.xml): with
 * "always" ticked once, a plug opens the app with the permission granted and no dialog (user: fewest dialogs).
 * Never logs a line: they carry the Wi-Fi credentials.
 */
class UsbLink(
    private val ctx: Context,
    private val onLine: (String) -> Unit,
    private val onUp: (Boolean) -> Unit,
) {
    companion object {
        const val VID = 0x1209
        const val PID = 0xABD1
        private const val ACTION_PERMISSION = "com.leakcam.setup.USB_PERMISSION"
        private const val TAG = "LeakcamUsb"
        private const val SET_CONTROL_LINE_STATE = 0x22
        private const val READ_TIMEOUT_MS = 200
        private const val WRITE_TIMEOUT_MS = 1000
        private const val MAX_LINE = 160            // k230_link.h LINK_MAX_LINE: a longer line is not ours
    }

    private val usb = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
    private val main = Handler(Looper.getMainLooper())
    private var device: UsbDevice? = null
    private var conn: UsbDeviceConnection? = null
    private var data: UsbInterface? = null
    private var epIn: UsbEndpoint? = null
    private var epOut: UsbEndpoint? = null
    @Volatile private var reading = false
    private var reader: Thread? = null
    private var asked = false                       // the permission dialog is open: not asked twice
    val up get() = conn != null

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            @Suppress("DEPRECATION")
            val d = i.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
            when (i.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> if (d != null && ours(d)) open(d)
                UsbManager.ACTION_USB_DEVICE_DETACHED -> if (d != null && d.deviceName == device?.deviceName) close()
                ACTION_PERMISSION -> {
                    asked = false
                    if (d != null && i.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) open(d)
                    else Log.i(TAG, "usb permission refused")
                }
            }
        }
    }

    fun start() {
        val f = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(ACTION_PERMISSION)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            ctx.registerReceiver(receiver, f, Context.RECEIVER_EXPORTED)   // attach/detach come from the system
        else
            ctx.registerReceiver(receiver, f)
        find()
    }

    fun stop() {
        try { ctx.unregisterReceiver(receiver) } catch (e: IllegalArgumentException) { }
        close()
    }

    /** a LEAKCAM already plugged in (the app opened by the plug, or started with the cable in) */
    fun find() {
        usb.deviceList.values.firstOrNull(::ours)?.let { open(it) }
    }

    private fun ours(d: UsbDevice) = d.vendorId == VID && d.productId == PID

    private fun open(d: UsbDevice) {
        if (conn != null)
            return
        if (!usb.hasPermission(d)) {
            if (!asked) {
                asked = true
                val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
                val pi = PendingIntent.getBroadcast(ctx, 0, Intent(ACTION_PERMISSION).setPackage(ctx.packageName), flags)
                usb.requestPermission(d, pi)
            }
            return
        }
        // the CDC data interface (class 0x0A) carries the bytes; the control one (0x02) gets DTR, as a PC's driver does
        var ctl: UsbInterface? = null
        for (i in 0 until d.interfaceCount) {
            val itf = d.getInterface(i)
            when (itf.interfaceClass) {
                UsbConstants.USB_CLASS_COMM -> ctl = itf
                UsbConstants.USB_CLASS_CDC_DATA -> {
                    data = itf
                    for (e in 0 until itf.endpointCount) {
                        val ep = itf.getEndpoint(e)
                        if (ep.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
                        if (ep.direction == UsbConstants.USB_DIR_IN) epIn = ep else epOut = ep
                    }
                }
            }
        }
        val itf = data
        val c = usb.openDevice(d)
        if (itf == null || epIn == null || epOut == null || c == null || !c.claimInterface(itf, true)) {
            Log.w(TAG, "usb open failed")
            c?.close()
            return
        }
        ctl?.let { c.controlTransfer(0x21, SET_CONTROL_LINE_STATE, 0x03, it.id, null, 0, WRITE_TIMEOUT_MS) }
        device = d
        conn = c
        reading = true
        reader = Thread(::readLoop, "usb-link").apply { start() }
        Log.i(TAG, "usb up")
        onUp(true)
    }

    private fun readLoop() {
        val buf = ByteArray(epIn?.maxPacketSize ?: 512)
        val line = StringBuilder()
        while (reading) {
            val c = conn ?: break
            val n = c.bulkTransfer(epIn, buf, buf.size, READ_TIMEOUT_MS)
            for (i in 0 until n) {
                val ch = buf[i].toInt().toChar()
                when {
                    ch == '\n' -> {
                        val l = line.toString().trimEnd('\r')
                        line.setLength(0)
                        main.post { onLine(l) }
                    }
                    line.length < MAX_LINE -> line.append(ch)
                }
            }
        }
    }

    /** one line to the board; false: no link or the write failed */
    fun write(l: String): Boolean {
        val c = conn ?: return false
        val b = (l + "\n").toByteArray(Charsets.US_ASCII)
        return c.bulkTransfer(epOut, b, b.size, WRITE_TIMEOUT_MS) == b.size
    }

    fun close() {
        val c = conn ?: return
        reading = false
        reader?.join(READ_TIMEOUT_MS * 3L)
        reader = null
        data?.let { c.releaseInterface(it) }
        c.close()
        conn = null
        device = null
        data = null
        epIn = null
        epOut = null
        Log.i(TAG, "usb down")
        main.post { onUp(false) }
    }
}
