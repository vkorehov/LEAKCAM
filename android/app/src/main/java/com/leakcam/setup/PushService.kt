package com.leakcam.setup

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.util.Log
import com.google.firebase.messaging.FirebaseMessaging
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage
import java.net.HttpURLConnection
import java.net.URL

/**
 * The server's push (Firebase Cloud Messaging, DESIGN.md 5.3.13): a leak opens the leak alert, a failed Wi-Fi change and
 * a camera out of focus (5.3.17) a normal notification; it arrives with the app closed. The phone's FCM token goes to the server at every start and on
 * every new token (PUT /v1/phones/<Android ID>/token).
 */
class PushService : FirebaseMessagingService() {
    override fun onMessageReceived(message: RemoteMessage) {
        val data = message.data
        Log.i(TAG, "push ${data["type"]} ${data["id"]}")
        // the person is tuning the focus or watching live: nothing about the cameras now (a hand at the lens, the
        // room's light are no news; the server holds these back too, this covers a push already on its way). Water at
        // the probe or the humidity alarm still ring
        if (FocusActivity.open && (data["type"] == "focus" || data["type"] == "media" ||
                                   data["type"] == "leak" && data["why"] == "camera change")) {
            Log.i(TAG, "push ${data["type"]} held back: the focus / live screen is open")
            return
        }
        when (data["type"]) {
            "leak" -> LeakAlert.show(this, data)
            "media" -> picture(data["id"].orEmpty(), data["media"].orEmpty())
            "wifi_failed" -> wifiFailed(this, data["id"].orEmpty(), data["ssid"].orEmpty())
            "focus" -> focus(this, data["id"].orEmpty(), data["cam"]?.toIntOrNull() ?: 0, data["why"].orEmpty(),
                             data["ssid"].orEmpty())
        }
    }

    /** a leak's first picture is on the server: into the gallery, then into the alert if it still rings (this runs
     *  on FCM's worker thread: the download may block, within FCM's ~10 s) */
    private fun picture(id: String, media: String) {
        val g = Gallery(this)
        val uri = g.save(media)
        if (uri == null) {
            Log.i(TAG, "media $media not on the server")
            return
        }
        g.thumbnail(uri)?.let { LeakAlert.picture(this, id, it) }
    }

    override fun onNewToken(token: String) = send(this, token)

    companion object {
        const val TAG = "LeakcamPush"
        const val WIFI_CHANNEL = "wifi"
        const val FOCUS_CHANNEL = "focus"

        fun register(context: Context) {
            FirebaseMessaging.getInstance().token.addOnSuccessListener { send(context, it) }
                .addOnFailureListener { Log.w(TAG, "no FCM token: ${it.message}") }
        }

        /** the phone as the server knows its user: its Android ID; a debug build on the bench can be another phone
         *  (`settings put global leakcam_phone_id <16 hex>`, test135: a phone with no boards, the person's list kept) */
        fun phoneId(context: Context): String {
            if (BuildConfig.DEBUG)
                android.provider.Settings.Global.getString(context.contentResolver, "leakcam_phone_id")
                    ?.takeIf { it.matches(Regex("[0-9a-f]{16}")) }?.let { return it }
            return android.provider.Settings.Secure.getString(context.contentResolver,
                                                              android.provider.Settings.Secure.ANDROID_ID).orEmpty()
        }

        private fun send(context: Context, token: String) {
            val phone = phoneId(context)
            Thread {
                try {
                    val c = URL("${BuildConfig.SERVER}/v1/phones/$phone/token").openConnection() as HttpURLConnection
                    c.requestMethod = "PUT"
                    c.doOutput = true
                    c.connectTimeout = 5000
                    c.readTimeout = 5000
                    c.setRequestProperty("Content-Type", "application/json")
                    c.outputStream.use { it.write(org.json.JSONObject().put("token", token).toString().toByteArray()) }
                    Log.i(TAG, "token sent: ${c.responseCode}")
                    c.disconnect()
                } catch (e: Exception) {
                    Log.w(TAG, "token not sent: ${e.message}")
                }
            }.start()
        }

        /** a camera out of focus or never tuned (DESIGN.md 5.3.17): a normal notification, not the leak alarm; a tap
         *  opens the focus screen on that board and camera */
        fun focus(context: Context, id: String, cam: Int, why: String, ssid: String) {
            val nm = context.getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(NotificationChannel(FOCUS_CHANNEL, context.getString(R.string.focus_channel),
                                                             NotificationManager.IMPORTANCE_DEFAULT))
            val open = android.app.PendingIntent.getActivity(context, ("focus$id$cam").hashCode(),
                android.content.Intent(context, FocusActivity::class.java)
                    .putExtra(FocusActivity.EXTRA_ID, id).putExtra(FocusActivity.EXTRA_CAM, cam)
                    .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK),
                android.app.PendingIntent.FLAG_IMMUTABLE or android.app.PendingIntent.FLAG_UPDATE_CURRENT)
            val where = ssid.ifEmpty { id }
            nm.notify(("focus$id$cam").hashCode(), Notification.Builder(context, FOCUS_CHANNEL)
                .setSmallIcon(R.drawable.ic_alert)
                .setContentTitle(context.getString(when (why) {
                    "untuned" -> R.string.focus_untuned_title
                    "dark" -> R.string.focus_dark_title
                    else -> R.string.focus_unfocused_title
                }, if (why == "untuned") Cams.inText(context, cam) else Cams.name(context, cam), where))
                .setContentText(context.getString(when (why) {
                    "untuned" -> R.string.focus_untuned_text
                    "dark" -> R.string.focus_dark_text
                    else -> R.string.focus_unfocused_text
                }))
                .setContentIntent(open)
                .setAutoCancel(true)
                .build())
        }

        /** a Wi-Fi change through the server failed: the board kept its old network (5.3.13) */
        fun wifiFailed(context: Context, id: String, ssid: String) {
            val nm = context.getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(NotificationChannel(WIFI_CHANNEL, context.getString(R.string.wifi_channel),
                                                             NotificationManager.IMPORTANCE_HIGH))
            nm.notify(("wifi$id").hashCode(), Notification.Builder(context, WIFI_CHANNEL)
                .setSmallIcon(R.drawable.ic_wifi)
                .setContentTitle(context.getString(R.string.wifi_failed_title, id))
                .setContentText(context.getString(R.string.wifi_failed_text, ssid))
                .setAutoCancel(true)
                .build())
        }
    }
}
