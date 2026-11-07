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
                text = intent.getStringExtra(LeakAlert.EXTRA_TEXT).orEmpty()
                textSize = 20f
                setTextColor(Color.WHITE)
                gravity = Gravity.CENTER
                setPadding(0, pad, 0, pad)
            })
            addView(Button(context).apply {
                setText(R.string.alert_ack)
                textSize = 22f
                setOnClickListener { acknowledge() }
            })
        })
        Log.i(TAG, "alert screen $id")
        registerReceiver(close, android.content.IntentFilter(LeakAlert.ACTION_CLOSE), RECEIVER_NOT_EXPORTED)
    }

    private val close = object : android.content.BroadcastReceiver() {
        override fun onReceive(c: android.content.Context, i: android.content.Intent) = finish()
    }

    override fun onDestroy() {
        unregisterReceiver(close)
        super.onDestroy()
    }

    /** the alarm off, then the app on this leak: the board picked, its pictures and video (user 2026-11-07: "after
     *  notification, open app and point to those pics") */
    private fun acknowledge() {
        LeakAlert.acknowledge(this, id)
        startActivity(android.content.Intent(this, MainActivity::class.java).apply {
            putExtra(LeakAlert.EXTRA_ID, id)
            putExtra(LeakAlert.EXTRA_TITLE, intent.getStringExtra(LeakAlert.EXTRA_TITLE))
            putExtra(LeakAlert.EXTRA_TEXT, intent.getStringExtra(LeakAlert.EXTRA_TEXT))
            putExtra(LeakAlert.EXTRA_MEDIA, intent.getStringExtra(LeakAlert.EXTRA_MEDIA))
            flags = android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_SINGLE_TOP
        })
        finish()
    }

    companion object {
        const val TAG = "LeakcamAlert"
    }
}
