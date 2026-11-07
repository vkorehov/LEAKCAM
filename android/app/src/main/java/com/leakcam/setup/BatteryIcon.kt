package com.leakcam.setup

import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.RectF
import android.graphics.drawable.Drawable

/**
 * The battery icon of a LEAKCAM's row (DESIGN.md 5.3.9, tests/test131): an outline filled to the charge, green while
 * the board is on USB power (charging), red at 15 % and below, grey otherwise; only the outline when the level is not
 * known yet. The level is the K230's last reading on battery (the board reports it on every channel: check-in, BLE
 * advertisement, the USB cable's ID); the percent goes on the view's tooltip (hover, long press).
 */
class BatteryIcon(private val percent: Int?, private val charging: Boolean, density: Float) : Drawable() {
    private val w = (24 * density).toInt()
    private val h = (24 * density).toInt()
    private val stroke = 2 * density
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = stroke
        color = 0xFF757575.toInt()
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = when {
            charging -> 0xFF2E7D32.toInt()
            percent != null && percent <= 15 -> 0xFFC62828.toInt()
            else -> 0xFF757575.toInt()
        }
    }

    override fun draw(canvas: Canvas) {
        // where it is placed (an icon strip draws several side by side: it drew over the first, 2026-11-07)
        canvas.save()
        canvas.translate(bounds.left.toFloat(), bounds.top.toFloat())
        // a horizontal cell: body 18 x 10 dp at the middle, a 2 dp terminal on the right
        val d = w / 24f
        val body = RectF(2 * d, 7 * d, 20 * d, 17 * d)
        canvas.drawRoundRect(body, 1.5f * d, 1.5f * d, outline)
        canvas.drawRect(RectF(20 * d, 10 * d, 22 * d, 14 * d), fill.takeIf { charging } ?: outline)
        if (percent != null && percent > 0) {
            val inner = RectF(body.left + stroke, body.top + stroke, body.right - stroke, body.bottom - stroke)
            inner.right = inner.left + inner.width() * percent.coerceIn(0, 100) / 100f
            canvas.drawRect(inner, fill)
        }
        canvas.restore()
    }

    override fun getIntrinsicWidth() = w
    override fun getIntrinsicHeight() = h
    override fun setAlpha(alpha: Int) {}
    override fun setColorFilter(colorFilter: ColorFilter?) {}
    @Deprecated("Deprecated in Java")
    override fun getOpacity() = PixelFormat.TRANSLUCENT

    companion object {
        /**
         * The charge from the cell voltage: a 1-cell Li-ion discharge curve (4.20 V full; the K230 reads it under its
         * own ~200 mA load, a little below rest), 3.10 V = 0 % (the K230's cut-off under load, DESIGN.md battery low)
         */
        private val CURVE = listOf(4200 to 100, 4100 to 90, 4000 to 78, 3900 to 63, 3800 to 48, 3700 to 30,
                                   3600 to 15, 3500 to 7, 3400 to 3, 3100 to 0)

        fun percent(mv: Int): Int {
            if (mv >= CURVE.first().first) return 100
            for (i in 1 until CURVE.size) {
                val (v1, p1) = CURVE[i - 1]
                val (v0, p0) = CURVE[i]
                if (mv >= v0) return p0 + (p1 - p0) * (mv - v0) / (v1 - v0)
            }
            return 0
        }
    }
}
