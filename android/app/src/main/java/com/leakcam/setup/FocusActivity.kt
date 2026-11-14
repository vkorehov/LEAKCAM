package com.leakcam.setup

import android.app.Activity
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.media.MediaCodec
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.RadioGroup
import android.media.MediaFormat
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import java.io.DataInputStream
import java.net.HttpURLConnection
import java.net.URL
import android.view.HapticFeedbackConstants
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.Button
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView

/**
 * "LEAKCAM Focus Adjustment" (DESIGN.md 5.3.17; user 2026-11-12: "separate screen for focus tuning per camera", "good UX
 * for user to adjust focus asap"; 2026-11-13: "focus tuning is done on already setup phone", "live stream video",
 * "toggle between two cameras", "quickly switch camera and see instantly video changing"). Opened from the status
 * screen's Tune focus (a flagged camera) or the board's ⋮ menu, over the phone's internet through the server, no cable:
 *   - POST /v1/devices/<id>/focus {"cam":n}: the board joins at its next check-in (60 s while flagged): a spinner and
 *     the moment it starts;
 *   - GET /v1/focus/<id>/live: the board's records as they come ([u32 BE length | 1 << 30 text][u64 BE us][payload]):
 *     CAM,<cam> before a camera's first picture, FOC,<cam>,<seq>,<score> once a second (FocusGuide: which way), FOCUS,OK,<cam>,<score> after Done,
 *     FOCUS,ERR,<cam>; an access unit of the shown camera's native H.265, decoded in hardware straight
 *     onto the screen;
 *   - POST /v1/focus/<id>/cmd: {"cam":n} (the camera buttons: the board switches at the next frame), {"done":<score>}
 *     (FocusGuide's best kept as the camera's reference), {"stop":true} (the screen left).
 * Layout (skill leakcam-app-ux): nothing moves: the camera switch, the video, the lens picture with the way to turn
 * (green, the other way red: ic_lens_cw / ic_lens_ccw, swapped when it gets blurrier; the first way random), two fixed
 * lines of text, the
 * bar, the slower hint (kept INVISIBLE), Done.
 */
class FocusActivity : Activity() {
    companion object {
        const val EXTRA_ID = "id"
        const val EXTRA_CAM = "cam"

        /** the focus adjustment or live video is on the screen: no notification about the cameras meanwhile
         *  (PushService; user 2026-11-13: "i dont want to be notified while tuning focus, nor ... viewing live video") */
        @Volatile var open = false
        const val EXTRA_LIVE = "live"                      // Live video (DESIGN.md 5.3.19): the same stream and camera
                                                           // switch, no lens guide, bar or Done
        private const val TAG = "LeakcamFocus"
        private const val RETRY_MS = 3000L                 // an ended or failed session: asked for again after this
        private const val STALL_MS = 8000L                 // live, and no record this long: the stream reconnected
        private const val TEXT = 1 shl 30                  // a record's kind bit (server/focus.go FocusText)
    }

    private enum class State { STARTING, LIVE, ENDED, FAILED, PAUSED }

    private var guide = FocusGuide()
    private var cam = 0
    private var board = ""                      // the board's ID (not `id`: inside a View's apply {} that is the view's id, -1)
    private val camButtons = arrayOfNulls<RadioButton>(2)
    private lateinit var video: SurfaceView
    @Volatile private var decoder: MediaCodec? = null
    @Volatile private var shownCam = 0                      // the reader thread's copy: frames of the other camera skipped
    private val outInfo = MediaCodec.BufferInfo()
    private lateinit var lens: ImageView
    private lateinit var spinner: ProgressBar
    private lateinit var say: TextView
    private lateinit var slower: TextView
    private lateinit var bar: Bar
    private lateinit var done: Button
    private var lastBest = 0.0
    private var camFailed = false
    @Volatile private var session: Any? = null             // the running begin() loop's token
    @Volatile private var lastRecord = 0L                  // when the stream's last record came (elapsedRealtime)
    private val stall = android.os.Handler(android.os.Looper.getMainLooper())
    private var liveOnly = false                            // opened as Live video (EXTRA_LIVE)
    private var state = State.STARTING
    private var wasLeft = false             // (not `left`: inside a View's apply {} that is the view's own) the screen was left once (Home, another app, the screen off): PAUSED on return
    private var startsAt = ""
    @Volatile private var live: HttpURLConnection? = null
    @Volatile private var running = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        board = intent.getStringExtra(EXTRA_ID).orEmpty()
        cam = intent.getIntExtra(EXTRA_CAM, 0)
        liveOnly = intent.getBooleanExtra(EXTRA_LIVE, false)
        // who opened it: a session switches the LEDs on and holds the board, so an opening nobody meant must be found
        Log.i(TAG, "opened for $board cam $cam live $liveOnly by $referrer flags 0x${Integer.toHexString(intent.flags)}")
        shownCam = cam
        val d = resources.displayMetrics.density
        val pad = (16 * d).toInt()
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            setBackgroundColor(Color.WHITE)
            addView(TextView(context).apply {
                setText(if (liveOnly) R.string.live_title else R.string.focus_title)
                textSize = 20f
            })
            // the camera choice: radio buttons, as the device list's (user 2026-11-13: "change those buttons to be radio
            // buttons ... same approach as device selection. same UX"; skill leakcam-app-ux 2f)
            addView(RadioGroup(context).apply {
                orientation = RadioGroup.HORIZONTAL
                for (c in 0..1)
                    addView(RadioButton(context).apply {
                        id = View.generateViewId()
                        text = Cams.name(context, c)
                        textSize = 16f
                        maxLines = 1
                        setOnClickListener { pick(c) }
                        camButtons[c] = this
                    }, RadioGroup.LayoutParams(0, -2, 1f))
            })
            video = SurfaceView(context).apply {                       // the camera's 4:3, the full width
                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceCreated(h: SurfaceHolder) = startDecoder(h)
                    override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) {}
                    override fun surfaceDestroyed(h: SurfaceHolder) = stopDecoder()
                })
            }
            val w = resources.displayMetrics.widthPixels - 2 * pad
            addView(video, LinearLayout.LayoutParams(-1, w * 3 / 4))
            addView(LinearLayout(context).apply {                      // the lens and which way, a spinner while waiting
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER
                spinner = ProgressBar(context).apply { visibility = View.INVISIBLE }
                addView(spinner, LinearLayout.LayoutParams((32 * d).toInt(), (32 * d).toInt()))
                lens = ImageView(context).apply {
                    setImageResource(R.drawable.ic_lens)
                    visibility = if (liveOnly) View.GONE else View.VISIBLE
                }
                addView(lens, LinearLayout.LayoutParams((130 * d).toInt(), (130 * d).toInt()))
                addView(View(context), LinearLayout.LayoutParams((32 * d).toInt(), 1))   // the spinner's balance
            }, LinearLayout.LayoutParams(-1, -2))
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
            if (!liveOnly)                                             // a dirty lens looks like a blurred one (user
                addView(TextView(context).apply {                      // 2026-11-13: "offer to wipe camera with alcohol")
                    setText(R.string.focus_wipe)
                    textSize = 14f
                    gravity = Gravity.CENTER
                    setTextColor(Color.GRAY)
                }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = pad / 2 })
            addView(View(context), LinearLayout.LayoutParams(-1, 0, 1f))
            done = Button(context).apply {
                setText(R.string.focus_done)
                isEnabled = false
                setOnClickListener {
                    if (state == State.PAUSED) {                       // Resume: the session asked for again
                        wasLeft = false
                        begin()
                    } else {
                        command("{\"done\":%.3f}".format(java.util.Locale.ROOT, guide.best))
                    }
                }
            }
            addView(done, LinearLayout.LayoutParams(-1, -2))
            if (liveOnly)                                              // the bottom button: only Resume there (show())
                for (v in listOf(bar, slower))
                    v.visibility = View.GONE
        })
        show()
    }

    override fun onStart() {
        super.onStart()
        open = true
        if (wasLeft) {
            state = State.PAUSED
            show()
        } else {
            begin()
        }
    }

    override fun onStop() {
        running = false
        session = null
        stall.removeCallbacksAndMessages(null)
        open = false
        command("{\"stop\":true}")
        Thread { live?.disconnect() }.start()
        super.onStop()
        // left (Home, another app, the screen off): the session is over; the screen stays (user 2026-11-14: "dont self
        // close focus screen") and comes back PAUSED: a new session only on Resume. Starting one by itself at every
        // return held the board with the LEDs on each time the app was opened again (found by test111)
        wasLeft = true
    }

    /** the session asked for and the live stream read, again and again while the screen is open: whatever ends it (the
     *  board's Stop or its time, a camera that failed, the server or the network away, a picture that stalls), the
     *  screen asks again RETRY_MS later and waits for the board: no state here needs the person to leave and come back
     *  (user 2026-11-13: "why screen never recovers ... self recovering on all screens"; skill leakcam-app-ux 8) */
    private fun begin() {
        state = State.STARTING
        running = true
        show()
        val mine = Any()
        session = mine                                      // one loop per start: a stopped screen's loop ends
        Thread {
            while (running && session === mine) {
                once()
                if (!running || session !== mine)
                    break
                Thread.sleep(RETRY_MS)                          // the end or the failure shown meanwhile
                if (running && session === mine)
                    runOnUiThread { state = State.STARTING; camFailed = false; guide = FocusGuide(); lastBest = 0.0; show() }
            }
        }.start()
        stall.removeCallbacksAndMessages(null)
        stall.postDelayed(object : Runnable {                   // a live picture that stopped coming: the stream cut,
            override fun run() {                                // so the loop above starts it again
                if (!running)
                    return
                if (state == State.LIVE && android.os.SystemClock.elapsedRealtime() - lastRecord > STALL_MS) {
                    Log.w(TAG, "no record for $STALL_MS ms: reconnecting")
                    Thread { live?.disconnect() }.start()
                }
                stall.postDelayed(this, 2000)
            }
        }, 2000)
    }

    private fun once() {
            try {
                val c = URL("${BuildConfig.SERVER}/v1/devices/$board/focus").openConnection() as HttpURLConnection
                c.requestMethod = "POST"
                c.doOutput = true
                c.connectTimeout = 5000
                c.readTimeout = 5000
                c.setRequestProperty("Content-Type", "application/json")
                c.outputStream.use { it.write("{\"cam\":$cam}".toByteArray()) }
                val j = org.json.JSONObject(c.inputStream.bufferedReader().readText())
                c.disconnect()
                val at = j.optLong("at", 0)
                val within = j.optInt("within_s", 60)
                val when_ = if (at > 0) java.text.DateFormat.getTimeInstance(java.text.DateFormat.SHORT).format(at * 1000)
                            else "within ${if (within >= 120) "${within / 60} min" else "$within s"}"
                runOnUiThread { startsAt = when_; show() }
                read()
            } catch (e: Exception) {
                Log.w(TAG, "focus session: $e")
                if (running) runOnUiThread { state = State.FAILED; show() }
            }
    }

    /** the board's records (blocking, its own thread): text lines to the guide, access units to the decoder */
    private fun read() {
        val c = URL("${BuildConfig.SERVER}/v1/focus/$board/live").openConnection() as HttpURLConnection
        c.connectTimeout = 5000
        c.readTimeout = 0                                  // waits for the board's check-in, then for its frames
        live = c
        DataInputStream(c.inputStream.buffered(65536)).use { din ->
            var from = -1                                  // the camera of the next picture: CAM,<cam> before its first
            while (running) {
                val l = din.readInt()
                lastRecord = android.os.SystemClock.elapsedRealtime()
                val us = din.readLong()
                val p = ByteArray(l and TEXT.inv())
                din.readFully(p)
                if (l and TEXT != 0) {
                    val line = String(p, Charsets.US_ASCII)
                    if (line.startsWith("CAM,"))
                        from = line.split(",")[1].toIntOrNull() ?: -1
                    runOnUiThread { onLine(line) }
                } else {
                    if (state != State.LIVE) runOnUiThread { state = State.LIVE; show() }
                    onFrame(if (from < 0) shownCam else from, us, p)   // the parameter sets: before any CAM
                }
            }
        }
        if (running) runOnUiThread { state = State.ENDED; show() }   // the board ended it (EOF ends readInt too)
    }

    /** a command for the board (the server keeps it for the board's next poll) */
    private fun command(json: String) = Thread {
        try {
            val c = URL("${BuildConfig.SERVER}/v1/focus/$board/cmd").openConnection() as HttpURLConnection
            c.requestMethod = "POST"
            c.doOutput = true
            c.connectTimeout = 5000
            c.readTimeout = 5000
            c.setRequestProperty("Content-Type", "application/json")
            c.outputStream.use { it.write(json.toByteArray()) }
            c.responseCode
            c.disconnect()
        } catch (e: Exception) {
            Log.w(TAG, "focus command: $e")
        }
    }.start()

    private fun pick(c: Int) {
        cam = c
        shownCam = c
        guide = FocusGuide()
        lastBest = 0.0
        camFailed = false
        command("{\"cam\":$c}")
        show()
    }

    private fun onLine(l: String) {
        val f = l.split(",")
        when {
            liveOnly -> if (f[0] == "FOC" && state != State.LIVE) { state = State.LIVE; show() }   // no guide
            f.size == 4 && f[0] == "FOC" && f[1].toIntOrNull() == cam -> {
                if (state != State.LIVE) state = State.LIVE
                val changed = guide.add(f[3].toDoubleOrNull() ?: return)
                if (guide.best > lastBest * 1.02 && lastBest > 0)
                    say.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)   // a new best: felt, not looked at
                lastBest = maxOf(lastBest, guide.best)
                if (changed && guide.say in setOf(FocusGuide.Say.WRONG_WAY, FocusGuide.Say.TOO_FAR))
                    say.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)   // turn the other way
                show()
            }
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

    /** the video's decoder on the screen's surface: H.265 at the cameras' native size; the parameter sets come with
     * every IDR (the first frame, each second, each switch) */
    private fun startDecoder(h: SurfaceHolder) {
        try {
            val c = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_HEVC)
            c.configure(MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_HEVC, 2592, 1936), h.surface, null, 0)
            c.start()
            decoder = c
        } catch (e: Exception) {
            Log.w("LeakcamFocus", "no H.265 decoder: $e")
        }
    }

    private fun stopDecoder() {
        val c = decoder ?: return
        decoder = null
        synchronized(outInfo) {                                     // not while onFrame feeds it
            try { c.stop() } catch (e: Exception) { }
            c.release()
        }
    }

    /** one access unit from the stream (the reader thread): in, and every decoded picture out to the screen */
    private fun onFrame(camOf: Int, us: Long, au: ByteArray) {
        if (camOf != shownCam)
            return
        synchronized(outInfo) {
            val c = decoder ?: return
            try {
                val i = c.dequeueInputBuffer(50_000)
                if (i >= 0) {
                    c.getInputBuffer(i)?.apply { clear(); put(au) }
                    c.queueInputBuffer(i, 0, au.size, us, 0)
                }
                while (true) {
                    val o = c.dequeueOutputBuffer(outInfo, 0)
                    if (o < 0) break
                    c.releaseOutputBuffer(o, true)                  // shown now: live, no clock to wait for
                }
            } catch (e: IllegalStateException) {
                Log.w("LeakcamFocus", "decoder: $e")
            }
        }
    }

    private var lensShown = R.drawable.ic_lens

    /** the lens picture: changed only when it changes (memory leakcam-redraw-only-on-change) */
    private fun lensImage(id: Int) {
        if (id != lensShown) {
            lens.setImageResource(id)
            lensShown = id
        }
    }

    /** the bottom button: Apply (the sharpest point found: its score kept as the camera's reference), or Resume after
     *  the screen was left; Live video has only Resume */
    private fun bottom() {
        val paused = state == State.PAUSED
        done.setText(if (paused) R.string.focus_resume else R.string.focus_done)
        done.isEnabled = paused || (state == State.LIVE && !camFailed && !liveOnly && guide.sharp)
        done.visibility = if (liveOnly && !paused) View.INVISIBLE else View.VISIBLE
    }

    private fun show() {
        for (c in 0..1)
            camButtons[c]?.isChecked = c == cam                        // the camera shown
        if (state != State.LIVE || camFailed) {                        // starting, ended, failed
            lensImage(R.drawable.ic_lens)                  // no direction yet / any more
            lens.alpha = if (state == State.STARTING && !camFailed) 1f else 0.4f
            say.text = when {
                camFailed -> getString(R.string.focus_nocam, Cams.name(this, cam))
                state == State.STARTING -> getString(R.string.focus_starting, startsAt)
                state == State.PAUSED -> getString(R.string.focus_paused)
                state == State.ENDED -> getString(R.string.focus_ended)
                else -> getString(R.string.focus_failed)
            }
            spinner.visibility = if (state == State.STARTING && !camFailed) View.VISIBLE else View.INVISIBLE
            bar.set(0.0, 0.0, false)
            slower.visibility = View.INVISIBLE
            bottom()
            return
        }
        spinner.visibility = View.INVISIBLE
        if (liveOnly) {
            say.text = getString(R.string.live_now, Cams.inText(this, cam))
            return
        }
        val way = getString(if (guide.turn == FocusGuide.Turn.CW) R.string.focus_cw else R.string.focus_ccw)
        lens.alpha = 1f
        lensImage(when (guide.turn) {                     // green the way to turn, red the other
            FocusGuide.Turn.CCW -> R.drawable.ic_lens_ccw
            FocusGuide.Turn.CW -> R.drawable.ic_lens_cw
            FocusGuide.Turn.HOLD -> R.drawable.ic_lens_hold
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
        bottom()
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
