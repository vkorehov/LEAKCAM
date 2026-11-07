package com.leakcam.setup

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.os.Build
import android.util.Log

/**
 * The leak alert, the app's most important use case (DESIGN.md 5.3.13): Android's government alerts come by cell
 * broadcast, which apps cannot use, so this is the closest an app gets - a full-screen alert over the lock screen
 * (AlertActivity, like an alarm clock), the alarm sound repeating until acknowledged (FLAG_INSISTENT), vibration, through
 * silent mode (the alarm stream) and Do Not Disturb (the channel bypasses it once the person allowed DND access).
 */
object LeakAlert {
    // a channel's sound is fixed once it exists: a new sound needs a new channel ID (the old one deleted)
    const val CHANNEL = "leak_alarm"
    private val OLD_CHANNELS = listOf("leak", "leak_alert")
    const val EXTRA_ID = "id"
    const val EXTRA_TEXT = "text"
    const val EXTRA_TITLE = "title"
    const val ACTION_TEST_ACK = "com.leakcam.setup.TEST_ACK"
    const val ACTION_CLOSE = "com.leakcam.setup.ALERT_CLOSE"   // the open alert screens close
    private const val TAG = "LeakcamAlert"

    /** one notification per board: its ID's hash, so a second leak of the same board replaces the first */
    private fun notificationId(id: String) = id.hashCode()

    fun channel(context: Context) {
        val nm = context.getSystemService(NotificationManager::class.java)
        OLD_CHANNELS.forEach { nm.deleteNotificationChannel(it) }
        if (nm.getNotificationChannel(CHANNEL) != null)
            return
        nm.createNotificationChannel(NotificationChannel(CHANNEL, context.getString(R.string.alert_channel),
                                                         NotificationManager.IMPORTANCE_HIGH).apply {
            description = context.getString(R.string.alert_channel_about)
            // the emergency alert's attention signal (853 + 960 Hz, WEA cadence: res/raw/leak_alert.wav), not the
            // phone's wake-up alarm (user, 2026-11-07: "sound is not the same as emergency alert")
            // by its name, not R.raw's number: the number moves when resources are added, and the channel keeps the URI
            // it was made with (the sound fell back to another one after a rebuild, 2026-11-07)
            setSound(android.net.Uri.parse("android.resource://${context.packageName}/raw/leak_alert"),
                     AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM)
                         .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
            enableVibration(true)
            vibrationPattern = longArrayOf(0, 800, 400, 800, 400, 800)
            lockscreenVisibility = Notification.VISIBILITY_PUBLIC
            setBypassDnd(true)          // takes effect only with the person's DND access (MainActivity asks at setup)
        })
    }

    /** the alert for one board from the server's message (DESIGN.md 5.3.13): which LEAKCAM (its ID), what found the
     *  water, its Wi-Fi network and signal (bars, as everywhere in the app), when */
    fun show(context: Context, data: Map<String, String>) {
        val id = data["id"] ?: "?"
        val why = when (data["why"]) {
            "probe wet" -> context.getString(R.string.alert_why_probe)
            "camera change" -> context.getString(R.string.alert_why_camera)
            else -> data["why"].orEmpty()
        }
        val bars = data["rssi"]?.toIntOrNull()?.let { r -> if (r >= -60) 4 else if (r >= -70) 3 else if (r >= -80) 2 else 1 }
        val ssid = data["ssid"].orEmpty()
        // which LEAKCAM: its network says where it is (the home, the office) better than its serial (user, 2026-11-07)
        val title = if (ssid.isNotEmpty()) context.getString(R.string.alert_title_ssid, ssid)
                    else context.getString(R.string.alert_title, id)
        val serial = context.getString(R.string.alert_serial, id) +
            (bars?.let { " · " + context.getString(R.string.alert_signal, it) } ?: "")
        val at = (data["at"]?.toLongOrNull()?.let { it * 1000 } ?: System.currentTimeMillis())
        val time = android.text.format.DateFormat.getTimeFormat(context).format(java.util.Date(at))
        show(context, id, title, listOf(why, time, serial).filter { it.isNotEmpty() }.joinToString("\n"))
    }

    private fun show(context: Context, id: String, title: String, text: String) {
        channel(context)
        val open = Intent(context, AlertActivity::class.java).apply {
            putExtra(EXTRA_ID, id)
            putExtra(EXTRA_TITLE, title)
            putExtra(EXTRA_TEXT, text)
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pi = PendingIntent.getActivity(context, notificationId(id), open,
                                           PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(context, CHANNEL)
            .setSmallIcon(R.drawable.ic_alert)
            .setContentTitle(title)
            .setContentText(text)
            .setStyle(Notification.BigTextStyle().bigText(text))
            .setCategory(Notification.CATEGORY_ALARM)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setOngoing(true)                       // only acknowledging ends it
            .setFullScreenIntent(pi, true)
            .setContentIntent(pi)
            .build()
        n.flags = n.flags or Notification.FLAG_INSISTENT   // the sound repeats until acknowledged
        context.getSystemService(NotificationManager::class.java).notify(notificationId(id), n)
        Log.i(TAG, "alert $id shown, full screen allowed ${fullScreenAllowed(context)}, dnd access ${dndAccess(context)}")
    }

    fun acknowledge(context: Context, id: String) = acknowledge(context, notificationId(id))

    private fun acknowledge(context: Context, nid: Int) {
        context.getSystemService(NotificationManager::class.java).cancel(nid)
        Log.i(TAG, "alert $nid acknowledged")
    }

    fun fullScreenAllowed(context: Context) = Build.VERSION.SDK_INT < 34 ||
        context.getSystemService(NotificationManager::class.java).canUseFullScreenIntent()

    fun dndAccess(context: Context) =
        context.getSystemService(NotificationManager::class.java).isNotificationPolicyAccessGranted

    /** the bench's trigger, debug builds only: `adb shell am broadcast -a com.leakcam.setup.TEST_ALERT -p
     *  com.leakcam.setup --es id <id> --es why "probe wet" --es ssid <ssid> --es rssi -55` (test133): the same message
     *  as the server's push */
    class TestTrigger : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (!BuildConfig.DEBUG)
                return
            if (intent.action == ACTION_TEST_ACK) {         // the bench silences it the moment its checks are done
                context.getSystemService(NotificationManager::class.java).activeNotifications
                    .filter { it.notification.channelId == CHANNEL }.forEach { acknowledge(context, it.id) }
                context.sendBroadcast(Intent(ACTION_CLOSE).setPackage(context.packageName))
                return
            }
            show(context, listOf("id", "why", "ssid", "rssi", "at").mapNotNull { k -> intent.getStringExtra(k)?.let { k to it } }
                .toMap())
        }
    }
}
