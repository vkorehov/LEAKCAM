package com.leakcam.setup

import android.app.Activity
import android.content.Context
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.os.Bundle
import android.util.Base64
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.View
import android.widget.Button
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView

/**
 * Focus tuning, one camera at a time (DESIGN.md 5.3.17, user 2026-11-12: "separate screen for focus tuning per camera",
 * "good UX for user to adjust focus asap"). Over the USB cable (the person is at the board turning the lens: no
 * check-in to wait for, ~15 scores a second): the K230 streams the camera's sharpness and a centre preview, FocusGuide
 * says which way to turn, Done saves the reached sharpness as the camera's reference (its later bad-focus check).
 *
 * The cable's lines (k230_link.h, answered by the K230 itself, not relayed to the BL616, after the board's PIN: the
 * preview is the camera's picture):
 *   phone -> board  PIN <6 digits>             the board's PIN (from the server, as the setup's) -> OK (the BL616's)
 *                   FOCUS,<cam>                start (or switch) streaming camera <cam> (0, 1): LEDs on, exposure locked
 *                   FOCUS,<cam>,DONE,<score>   keep <score> as the camera's focus reference
 *                   FOCUS,STOP                 end: LEDs off, the leak job's own exposure again
 *   board -> phone  FOC,<cam>,<seq>,<score>    one frame's sharpness (brightness-normalised, any unit: compared only
 *                                              with itself)
 *                   FIMG,<cam>,<seq>,<part>,<parts>,<base64>   the preview JPEG (the centre crop), in line-sized parts
 *                   FOCUS,OK,<cam>,<score>     the reference saved
 *                   FOCUS,ERR,<cam>            the camera gave no frame
 *                   ERR AUTH                   a FOCUS line before the PIN
 * Layout (skill leakcam-app-ux): nothing moves: the camera switch, the preview, the arrow, two fixed lines of text,
 * the bar, the slower hint (kept INVISIBLE), Done.
 */
class FocusActivity : Activity() {
    companion object {
        const val EXTRA_ID = "id"
        const val EXTRA_CAM = "cam"
    }

    private lateinit var link: UsbLink
    private var guide = FocusGuide()
    private var cam = 0
    private var id = ""
    private val camButtons = arrayOfNulls<Button>(2)
    private lateinit var preview: ImageView
    private lateinit var arrow: TextView
    private lateinit var say: TextView
    private lateinit var slower: TextView
    private lateinit var bar: Bar
    private lateinit var done: Button
    private var parts = arrayOfNulls<String>(0)
    private var partsSeq = -1L
    private var lastBest = 0.0
    private var authed = false                  // the board said OK to the PIN: FOCUS lines are answered
    private var pinFailed = false
    private var camFailed = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        id = intent.getStringExtra(EXTRA_ID).orEmpty()
        cam = intent.getIntExtra(EXTRA_CAM, 0)
        val d = resources.displayMetrics.density
        val pad = (16 * d).toInt()
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            setBackgroundColor(Color.WHITE)
            addView(TextView(context).apply {
                text = getString(R.string.focus_title, id)
                textSize = 20f
            })
            addView(LinearLayout(context).apply {                       // the camera switch
                orientation = LinearLayout.HORIZONTAL
                for (c in 0..1)
                    addView(Button(context).apply {
                        text = getString(R.string.focus_camera, c + 1)
                        maxLines = 1
                        setOnClickListener { pick(c) }
                        camButtons[c] = this
                    }, LinearLayout.LayoutParams(0, -2, 1f))
            })
            preview = ImageView(context).apply {
                scaleType = ImageView.ScaleType.CENTER_CROP
                setBackgroundColor(Color.rgb(0xF5, 0xF5, 0xF5))
            }
            addView(preview, LinearLayout.LayoutParams(-1, (220 * d).toInt()))
            arrow = TextView(context).apply {
                textSize = 72f
                gravity = Gravity.CENTER
            }
            addView(arrow, LinearLayout.LayoutParams(-1, -2))
            say = TextView(context).apply {
                textSize = 18f
                gravity = Gravity.CENTER
                minLines = 2
                maxLines = 2
            }
            addView(say, LinearLayout.LayoutParams(-1, -2))
            bar = Bar(context)
            addView(bar, LinearLayout.LayoutParams(-1, (28 * d).toInt()).apply { topMargin = pad })
            slower = TextView(context).apply {
                setText(R.string.focus_slower)
                textSize = 16f
                gravity = Gravity.CENTER
                setTextColor(Color.rgb(0xE6, 0x51, 0x00))
                visibility = View.INVISIBLE
            }
            addView(slower, LinearLayout.LayoutParams(-1, -2))
            addView(View(context), LinearLayout.LayoutParams(-1, 0, 1f))
            done = Button(context).apply {
                setText(R.string.focus_done)
                isEnabled = false
                setOnClickListener { link.write("FOCUS,$cam,DONE,%.3f".format(java.util.Locale.ROOT, guide.best)) }
            }
            addView(done, LinearLayout.LayoutParams(-1, -2))
        })
        link = UsbLink(this, ::onLine, ::onUp)
        show()
    }

    override fun onStart() {
        super.onStart()
        link.start()
    }

    override fun onStop() {
        link.write("FOCUS,STOP")
        link.stop()
        super.onStop()
    }

    private fun onUp(up: Boolean) {
        authed = false
        pinFailed = false
        if (up)
            unlock()
        show()
    }

    /** the board's PIN from the server (as the setup gets it), then PIN <pin> on the cable: its OK starts the session */
    private fun unlock() {
        val phone = PushService.phoneId(this)
        Thread {
            val pin = try {
                val c = java.net.URL("${BuildConfig.SERVER}/v1/devices/$id/pin?phone=$phone").openConnection()
                    as java.net.HttpURLConnection
                c.connectTimeout = 3000
                c.readTimeout = 3000
                val j = org.json.JSONObject(c.inputStream.bufferedReader().use { it.readText() })
                c.disconnect()
                j.getString("pin").takeIf { it.length == 6 && it.all(Char::isDigit) } ?: ""
            } catch (e: Exception) {
                ""
            }
            runOnUiThread {
                if (pin.isEmpty() || !link.write("PIN $pin")) {
                    pinFailed = true
                    show()
                }
            }
        }.start()
    }

    private fun pick(c: Int) {
        cam = c
        guide = FocusGuide()
        lastBest = 0.0
        camFailed = false
        preview.setImageDrawable(null)
        if (authed)
            link.write("FOCUS,$cam")
        show()
    }

    private fun onLine(l: String) {
        if (!authed) {                                              // the PIN's answer (the BL616's words)
            when (l) {
                "OK" -> {
                    authed = true
                    link.write("FOCUS,$cam")
                }
                "ERR PIN", "ERR LOCKED", "ERR AUTH" -> pinFailed = true
                else -> return
            }
            return show()
        }
        val f = l.split(",")
        when {
            f.size == 4 && f[0] == "FOC" && f[1].toIntOrNull() == cam -> {
                val changed = guide.add(f[3].toDoubleOrNull() ?: return)
                if (guide.best > lastBest * 1.02 && lastBest > 0)
                    say.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)   // a new best: felt, not looked at
                lastBest = maxOf(lastBest, guide.best)
                if (changed && guide.say in setOf(FocusGuide.Say.WRONG_WAY, FocusGuide.Say.TOO_FAR))
                    say.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)   // turn the other way
                show()
            }
            f.size == 6 && f[0] == "FIMG" && f[1].toIntOrNull() == cam -> image(f)
            f.size == 3 && f[0] == "FOCUS" && f[1] == "ERR" && f[2].toIntOrNull() == cam -> {
                camFailed = true
                show()
            }
            f.size == 4 && f[0] == "FOCUS" && f[1] == "OK" && f[2].toIntOrNull() == cam -> {
                say.setText(R.string.focus_saved)
                done.isEnabled = false
                if (cam == 0) pick(1) else finish()                     // camera 2 next, then back
            }
        }
    }

    /** a preview part; the whole JPEG shown when its last part came */
    private fun image(f: List<String>) {
        val seq = f[2].toLongOrNull() ?: return
        val part = f[3].toIntOrNull() ?: return
        val n = f[4].toIntOrNull() ?: return
        if (seq != partsSeq || parts.size != n) {
            partsSeq = seq
            parts = arrayOfNulls(n)
        }
        if (part !in 0 until n)
            return
        parts[part] = f[5]
        if (parts.any { it == null })
            return
        val jpeg = Base64.decode(parts.joinToString(""), Base64.DEFAULT)
        BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size)?.let { preview.setImageBitmap(it) }
    }

    private fun show() {
        for (c in 0..1)
            camButtons[c]?.isSelected = c == cam
        if (!link.up) {
            arrow.text = "⏚"                                       // a plug-like sign: waiting for the cable
            arrow.setTextColor(Color.GRAY)
            say.setText(R.string.focus_plug)
            bar.set(0.0, 0.0, false)
            slower.visibility = View.INVISIBLE
            done.isEnabled = false
            return
        }
        if (!authed || camFailed) {                                    // unlocking, or what stops it
            arrow.text = if (pinFailed || camFailed) "!" else "…"
            arrow.setTextColor(if (pinFailed || camFailed) Color.rgb(0xE6, 0x51, 0x00) else Color.GRAY)
            say.text = when {
                camFailed -> getString(R.string.focus_nocam, cam + 1)
                pinFailed -> getString(R.string.focus_no_pin)
                else -> getString(R.string.focus_pin)
            }
            bar.set(0.0, 0.0, false)
            slower.visibility = View.INVISIBLE
            done.isEnabled = false
            return
        }
        val way = getString(if (guide.turn == FocusGuide.Turn.CW) R.string.focus_cw else R.string.focus_ccw)
        arrow.text = when (guide.turn) {
            FocusGuide.Turn.CCW -> "↺"
            FocusGuide.Turn.CW -> "↻"
            FocusGuide.Turn.HOLD -> "✓"
        }
        arrow.setTextColor(when (guide.say) {
            FocusGuide.Say.STOP -> Color.rgb(0x2E, 0x7D, 0x32)
            FocusGuide.Say.WRONG_WAY, FocusGuide.Say.TOO_FAR -> Color.rgb(0xE6, 0x51, 0x00)
            else -> Color.rgb(0x15, 0x65, 0xC0)
        })
        say.text = when (guide.say) {
            FocusGuide.Say.START -> getString(R.string.focus_start, way)
            FocusGuide.Say.KEEP_GOING -> getString(R.string.focus_keep, way)
            FocusGuide.Say.WRONG_WAY -> getString(R.string.focus_wrong, way)
            FocusGuide.Say.TOO_FAR -> getString(R.string.focus_too_far, way)
            FocusGuide.Say.STOP -> getString(R.string.focus_stop)
            FocusGuide.Say.NO_DETAIL -> getString(R.string.focus_no_detail)
        }
        slower.visibility = if (guide.slower) View.VISIBLE else View.INVISIBLE
        bar.set(guide.score, guide.best, guide.sharp)
        done.isEnabled = guide.sharp
    }

    /** the sharpness now (filled) against the best so far (a marker): full width = the best + 15 % */
    private class Bar(c: Context) : View(c) {
        private var now = 0.0
        private var best = 0.0
        private var sharp = false
        private val fill = Paint()
        private val mark = Paint().apply { color = Color.BLACK; strokeWidth = 6f }
        private val back = Paint().apply { color = Color.rgb(0xEE, 0xEE, 0xEE) }

        fun set(now: Double, best: Double, sharp: Boolean) {
            if (now == this.now && best == this.best && sharp == this.sharp)
                return                                                  // redraw only on change (skill rule)
            this.now = now
            this.best = best
            this.sharp = sharp
            invalidate()
        }

        override fun onDraw(canvas: Canvas) {
            val w = width.toFloat()
            val h = height.toFloat()
            canvas.drawRect(0f, 0f, w, h, back)
            if (best <= 0)
                return
            val full = best * 1.15
            fill.color = if (sharp) Color.rgb(0x2E, 0x7D, 0x32) else Color.rgb(0x15, 0x65, 0xC0)
            canvas.drawRect(0f, 0f, (w * now / full).toFloat().coerceIn(0f, w), h, fill)
            val x = (w * best / full).toFloat()
            canvas.drawLine(x, 0f, x, h, mark)
        }
    }
}
