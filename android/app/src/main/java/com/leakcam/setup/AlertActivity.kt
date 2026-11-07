package com.leakcam.setup

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

/** the leak alert's full screen, shown over the lock screen (DESIGN.md 5.3.13); acknowledging stops the alarm */
class AlertActivity : Activity() {
    private var id = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setShowWhenLocked(true)
        setTurnScreenOn(true)
        id = intent.getStringExtra(LeakAlert.EXTRA_ID).orEmpty()
        val pad = (24 * resources.displayMetrics.density).toInt()
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(pad, pad, pad, pad)
            setBackgroundColor(Color.rgb(176, 0, 32))
            addView(TextView(context).apply {
                text = intent.getStringExtra(LeakAlert.EXTRA_TITLE) ?: getString(R.string.alert_title, id)
                textSize = 32f
                setTextColor(Color.WHITE)
                gravity = Gravity.CENTER
            })
            addView(TextView(context).apply {
                text = intent.getCharSequenceExtra(LeakAlert.EXTRA_TEXT) ?: ""
                textSize = 20f
                setTextColor(Color.WHITE)
                gravity = Gravity.CENTER
                setPadding(0, pad, 0, pad)
            })
            // the leak's first picture once the board has uploaded it (user 2026-11-07), as in the notification
            addView(android.widget.ImageView(context).apply {
                id = R.id.alert_picture
                adjustViewBounds = true
                visibility = android.view.View.GONE
                setPadding(0, 0, 0, pad)
            }, LinearLayout.LayoutParams(-1, -2))
            addView(Button(context).apply {
                setText(R.string.alert_ack)
                textSize = 22f
                setOnClickListener { acknowledge() }
            })
        })
        Log.i(TAG, "alert screen $id")
        registerReceiver(close, android.content.IntentFilter(LeakAlert.ACTION_CLOSE), RECEIVER_NOT_EXPORTED)
        registerReceiver(pictureReady, android.content.IntentFilter(LeakAlert.ACTION_PICTURE), RECEIVER_NOT_EXPORTED)
        showPicture()                      // already there when the screen opens late
    }

    private val pictureReady = object : android.content.BroadcastReceiver() {
        override fun onReceive(c: android.content.Context, i: android.content.Intent) {
            if (i.getStringExtra(LeakAlert.EXTRA_ID) == id) showPicture()
        }
    }

    /** the record's first picture from the gallery (the push service saved it): only one already saved, no download */
    private fun showPicture() {
        val first = intent.getStringExtra(LeakAlert.EXTRA_MEDIA).orEmpty().split(",").firstOrNull { it.endsWith("-p1") }
            ?: return
        Thread {
            val g = Gallery(this)
            val thumb = g.saved(first)?.let { g.thumbnail(it) } ?: return@Thread
            runOnUiThread {
                findViewById<android.widget.ImageView>(R.id.alert_picture).apply {
                    setImageBitmap(thumb)
                    visibility = android.view.View.VISIBLE
                }
            }
        }.start()
    }

    private val close = object : android.content.BroadcastReceiver() {
        override fun onReceive(c: android.content.Context, i: android.content.Intent) = finish()
    }

    override fun onDestroy() {
        unregisterReceiver(close)
        unregisterReceiver(pictureReady)
        super.onDestroy()
    }

    /** the alarm off, then the app on this leak: the board picked, its pictures and video (user 2026-11-07: "after
     *  notification, open app and point to those pics") */
    private fun acknowledge() {
        LeakAlert.acknowledge(this, id)
        startActivity(android.content.Intent(this, MainActivity::class.java).apply {
            putExtra(LeakAlert.EXTRA_ID, id)
            putExtra(LeakAlert.EXTRA_TITLE, intent.getStringExtra(LeakAlert.EXTRA_TITLE))
            putExtra(LeakAlert.EXTRA_TEXT, intent.getCharSequenceExtra(LeakAlert.EXTRA_TEXT))
            putExtra(LeakAlert.EXTRA_MEDIA, intent.getStringExtra(LeakAlert.EXTRA_MEDIA))
            flags = android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_SINGLE_TOP
        })
        finish()
    }

    companion object {
        const val TAG = "LeakcamAlert"
    }
}
