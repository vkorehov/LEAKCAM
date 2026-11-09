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
    // a channel's sound is fixed once it exists: a new sound needs a new channel ID (the old one deleted). So is its
    // Do Not Disturb bypass: setBypassDnd(true) counts only if the app has DND access when the channel is first made,
    // and a channel deleted and made again comes back with its old settings - an alert channel made before the person
    // allowed DND access never got through DND (the bench phone, 2026-11-09: mBypassDnd=false after a reinstall with
    // the access denied). So the channel ID depends on the access: "_dnd" once granted, made then with the bypass
    private const val BASE = "leak_alarm"
    // the bench's quiet nights (user 2026-11-07: "i want quiet night"): debug builds with `settings put global
    // leakcam_quiet 1` post the alert on a channel without sound or vibration; everything else the same
    private const val QUIET_BASE = "leak_alarm_quiet"
    private val OLD_CHANNELS = listOf("leak", "leak_alert")
    fun channelId(context: Context) = if (dndAccess(context)) "${BASE}_dnd" else BASE
    fun quietChannelId(context: Context) = if (dndAccess(context)) "${QUIET_BASE}_dnd" else QUIET_BASE
    private val ALL = listOf(BASE, "${BASE}_dnd", QUIET_BASE, "${QUIET_BASE}_dnd")
    const val EXTRA_ID = "id"
    const val EXTRA_TEXT = "text"
    const val EXTRA_TITLE = "title"
    const val EXTRA_MEDIA = "media"       // the new pictures and video by ID ("<device>-<s>-p1,..."): one tile each
    const val ACTION_TEST_ACK = "com.leakcam.setup.TEST_ACK"
    const val ACTION_CLOSE = "com.leakcam.setup.ALERT_CLOSE"   // the open alert screens close
    const val ACTION_PICTURE = "com.leakcam.setup.ALERT_PICTURE"   // the leak's first picture is in the gallery
    private const val TAG = "LeakcamAlert"

    /** one notification per board: its ID's hash, so a second leak of the same board replaces the first */
    private fun notificationId(id: String) = id.hashCode()

    fun quiet(context: Context) = BuildConfig.DEBUG &&
        android.provider.Settings.Global.getInt(context.contentResolver, "leakcam_quiet", 0) == 1

    fun channel(context: Context) {
        val nm = context.getSystemService(NotificationManager::class.java)
        OLD_CHANNELS.forEach { nm.deleteNotificationChannel(it) }
        val main = channelId(context)
        val quietId = quietChannelId(context)
        ALL.filter { it != main && it != quietId }.forEach { nm.deleteNotificationChannel(it) }  // the other access's
        if (quiet(context) && nm.getNotificationChannel(quietId) == null)
            nm.createNotificationChannel(NotificationChannel(quietId, "Leak alerts (bench, quiet)",
                                                             NotificationManager.IMPORTANCE_HIGH).apply {
                setSound(null, null)
                enableVibration(false)
                lockscreenVisibility = Notification.VISIBILITY_PUBLIC
                setBypassDnd(true)
            })
        if (nm.getNotificationChannel(main) != null)
            return
        nm.createNotificationChannel(NotificationChannel(main, context.getString(R.string.alert_channel),
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

    /** the alert for one board from the server's message (DESIGN.md 5.3.13): which LEAKCAM (its network, its ID),
     *  what found the water - the probe or the camera's inference, each its own words with that word in bold (user
     *  2026-11-07) - and when; no signal strength (user 2026-11-07) */
    fun show(context: Context, data: Map<String, String>) {
        val id = data["id"] ?: "?"
        val camera = data["why"] == "camera change"
        val why: CharSequence = when (data["why"]) {
            "probe wet" -> context.getText(R.string.alert_why_probe)
            "camera change" -> context.getText(R.string.alert_why_camera)
            else -> data["why"].orEmpty()
        }
        val ssid = data["ssid"].orEmpty()
        // which LEAKCAM: its network says where it is (the home, the office) better than its serial (user, 2026-11-07)
        val title = when {
            ssid.isEmpty() -> context.getString(R.string.alert_title, id)
            camera -> context.getString(R.string.alert_title_camera_ssid, ssid)
            else -> context.getString(R.string.alert_title_ssid, ssid)
        }
        val at = (data["at"]?.toLongOrNull()?.let { it * 1000 } ?: System.currentTimeMillis())
        val time = android.text.format.DateFormat.getTimeFormat(context).format(java.util.Date(at))
        val text = android.text.SpannableStringBuilder(why).append("\n").append(time).append("\n")
            .append(context.getString(R.string.alert_serial, id))
        show(context, id, title, text, data["media"].orEmpty(), null)
    }

    /** the leak's first picture arrived (the server's `media` push): the alert still ringing drawn again with it
     *  (user 2026-11-07: "one picture downloaded and embedded"); nothing once acknowledged. The update starts the
     *  sound over: keeping it quiet would stop the ringing alarm */
    fun picture(context: Context, id: String, picture: android.graphics.Bitmap) {
        val sbn = context.getSystemService(NotificationManager::class.java).activeNotifications
            .firstOrNull { it.id == notificationId(id) && it.notification.channelId in ALL }
            ?: return
        val x = sbn.notification.extras
        context.sendBroadcast(Intent(ACTION_PICTURE).setPackage(context.packageName).putExtra(EXTRA_ID, id))
        show(context, id, x.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty(),
             x.getCharSequence(EXTRA_TEXT) ?: "", x.getString(EXTRA_MEDIA).orEmpty(), picture)
    }

    private fun show(context: Context, id: String, title: String, text: CharSequence, media: String,
                     picture: android.graphics.Bitmap?) {
        channel(context)
        val open = Intent(context, AlertActivity::class.java).apply {
            putExtra(EXTRA_ID, id)
            putExtra(EXTRA_TITLE, title)
            putExtra(EXTRA_TEXT, text)
            putExtra(EXTRA_MEDIA, media)
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pi = PendingIntent.getActivity(context, notificationId(id), open,
                                           PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(context, if (quiet(context)) quietChannelId(context) else channelId(context))
            .setSmallIcon(R.drawable.ic_alert)
            .setContentTitle(title)
            .setContentText(text)
            .setStyle(if (picture == null) Notification.BigTextStyle().bigText(text)
                      else Notification.BigPictureStyle().bigPicture(picture).setSummaryText(text))
            .apply { if (picture != null) setLargeIcon(picture) }
            .addExtras(android.os.Bundle().apply {    // for the update with the picture
                putCharSequence(EXTRA_TEXT, text)
                putString(EXTRA_MEDIA, media)
            })
            .setCategory(Notification.CATEGORY_ALARM)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setOngoing(true)                       // only acknowledging ends it
            .setFullScreenIntent(pi, true)
            .setContentIntent(pi)
            .build()
        n.flags = n.flags or Notification.FLAG_INSISTENT   // the sound repeats until acknowledged
        context.getSystemService(NotificationManager::class.java).notify(notificationId(id), n)
        Log.i(TAG, "alert $id shown${if (picture != null) " with its picture" else ""}, full screen allowed ${fullScreenAllowed(context)}, dnd access ${dndAccess(context)}")
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
                // every notification of the app, not only the alerts: a Wi-Fi result (test134) or a leak alert from a
                // leak check's change (test108) stayed on the bench phone (user 2026-11-09: "hanging notification")
                context.getSystemService(NotificationManager::class.java).activeNotifications
                    .forEach { acknowledge(context, it.id) }
                context.sendBroadcast(Intent(ACTION_CLOSE).setPackage(context.packageName))
                return
            }
            show(context, listOf("id", "why", "ssid", "rssi", "at", "media").mapNotNull { k -> intent.getStringExtra(k)?.let { k to it } }
                .toMap())
        }
    }
}
