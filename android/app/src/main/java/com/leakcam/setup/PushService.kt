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
 * The server's push (Firebase Cloud Messaging, DESIGN.md 5.3.13): a leak opens the leak alert, a failed Wi-Fi change a
 * normal notification; it arrives with the app closed. The phone's FCM token goes to the server at every start and on
 * every new token (PUT /v1/phones/<Android ID>/token).
 */
class PushService : FirebaseMessagingService() {
    override fun onMessageReceived(message: RemoteMessage) {
        val data = message.data
        Log.i(TAG, "push ${data["type"]} ${data["id"]}")
        when (data["type"]) {
            "leak" -> LeakAlert.show(this, data)
            "media" -> picture(data["id"].orEmpty(), data["media"].orEmpty())
            "wifi_failed" -> wifiFailed(this, data["id"].orEmpty(), data["ssid"].orEmpty())
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
