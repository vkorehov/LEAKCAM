package com.leakcam.setup

import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.util.Log
import android.util.Size
import android.widget.FrameLayout
import android.widget.ImageView
import java.net.HttpURLConnection
import java.net.URL

/**
 * The leak panel's pictures and video (DESIGN.md 5.3.13): each by the ID the push announced, downloaded from the server
 * (GET /v1/media/<id>) into the phone's gallery (Pictures/LEAKCAM, Movies/LEAKCAM, saved once: the ID kept in the entry's
 * description; a readable name with the moment it was taken, and that moment as the gallery's date taken: user
 * 2026-11-12, "timestamp in picture naming, also in metadata", "user readable, not unix") - the
 * phone's own viewer and player show them, zoom, share and back them up (user 2026-11-07: "why not store in gallery?").
 * The leak panel shows one preview (the first file there) with "+N more": a tap on the preview opens that file alone
 * (view()), on "+N more" the gallery (open()). A file not on the server yet is asked for again every 5 s while the
 * panel is on the screen. The push service saves the leak's first picture for the alert (save()).
 */
class Gallery(private val context: Context) {
    private val main = Handler(Looper.getMainLooper())

    /** one picture or video into the gallery; ready(uri, thumbnail) once it is there. `on` is the view that needs it:
     *  asked again only while it is on the screen (no polling in the background) */
    fun fetch(id: String, on: android.view.View, ready: (Uri, Bitmap?) -> Unit) {
        val video = id.substringAfterLast('-').startsWith("v")
        Thread {
            val uri = save(id)
            if (uri != null) {
                val thumb = thumbnail(uri)
                main.post { ready(uri, thumb) }
            } else
                main.postDelayed({ if (on.isAttachedToWindow) fetch(id, on, ready) }, RETRY_MS)
        }.start()
    }

    /** the gallery's copy of this ID, downloaded first when it is not there yet; null while the server has none.
     *  Blocking: not on the main thread */
    fun save(id: String): Uri? {
        val video = id.substringAfterLast('-').startsWith("v")
        return saved(id, video) ?: download(id, video)
    }

    private fun collection(video: Boolean): Uri =
        if (video) MediaStore.Video.Media.EXTERNAL_CONTENT_URI else MediaStore.Images.Media.EXTERNAL_CONTENT_URI

    /** the name before 2026-11-12: the ID itself (files saved then are still found by it) */
    private fun name(id: String, video: Boolean) = id + if (video) ".mp4" else ".jpg"

    /** the readable name: LEAKCAM_<serial>_<yyyy-MM-dd_HH-mm-ss, phone time>_cam<N>_<shot>.jpg|mp4. The ID's end gives
     *  camera and shot (server mediaIDs: picture n is camera (n-1) % 2, its shot (n-1) / 2 + 1; video n is camera n-1) */
    private fun readableName(id: String, video: Boolean, takenMs: Long): String {
        val n = id.substringAfterLast('-').drop(1).toIntOrNull() ?: 1
        val cam = if (video) n - 1 else (n - 1) % CAMS
        val shot = if (video) 1 else (n - 1) / CAMS + 1
        val at = java.text.SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", java.util.Locale.US).format(java.util.Date(takenMs))
        return "LEAKCAM_${id.take(12)}_${at}_cam${cam}_$shot" + if (video) ".mp4" else ".jpg"
    }

    /** the gallery's copy of this ID if it was saved before, no download */
    fun saved(id: String): Uri? = saved(id, id.substringAfterLast('-').startsWith("v"))

    /** the gallery's copy of this ID, if it was saved before (one copy per ID): by the ID in its description, or a
     *  file saved before 2026-11-12 by its old name, the ID */
    private fun saved(id: String, video: Boolean): Uri? {
        val desc = if (video) MediaStore.Video.VideoColumns.DESCRIPTION else MediaStore.Images.ImageColumns.DESCRIPTION
        return context.contentResolver.query(collection(video), arrayOf(MediaStore.MediaColumns._ID),
                                             "$desc = ? OR ${MediaStore.MediaColumns.DISPLAY_NAME} = ?",
                                             arrayOf(id, name(id, video)), null)
            ?.use { c -> if (c.moveToFirst()) android.content.ContentUris.withAppendedId(collection(video), c.getLong(0)) else null }
    }

    /** the file from the server straight into the gallery's LEAKCAM album; null while the server has none (404) */
    private fun download(id: String, video: Boolean): Uri? {
        if (Build.VERSION.SDK_INT < 29)
            return null                     // the album needs Android 10's MediaStore paths (no storage permission)
        return try {
            val c = URL("${BuildConfig.SERVER}/v1/media/$id").openConnection() as HttpURLConnection
            c.connectTimeout = 5000
            c.readTimeout = 30000
            if (c.responseCode != 200) {
                c.disconnect()
                return null
            }
            // when it was taken: the board's capture time (the server's X-Taken), else the ID's (the request's) second
            val taken = c.getHeaderField("X-Taken")?.toLongOrNull()
                ?: (id.split('-').getOrNull(1)?.toLongOrNull()?.times(1000) ?: System.currentTimeMillis())
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, readableName(id, video, taken))
                put(if (video) MediaStore.Video.VideoColumns.DESCRIPTION else MediaStore.Images.ImageColumns.DESCRIPTION, id)
                put(MediaStore.MediaColumns.DATE_TAKEN, taken)
                put(MediaStore.MediaColumns.MIME_TYPE, if (video) "video/mp4" else "image/jpeg")
                put(MediaStore.MediaColumns.RELATIVE_PATH,
                    (if (video) Environment.DIRECTORY_MOVIES else Environment.DIRECTORY_PICTURES) + "/LEAKCAM")
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            }
            val r = context.contentResolver
            val uri = r.insert(collection(video), values) ?: return null
            r.openOutputStream(uri)?.use { out -> c.inputStream.use { it.copyTo(out) } }
            c.disconnect()
            r.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null)
            Log.i(TAG, "media $id saved to the gallery")
            uri
        } catch (e: Exception) {
            Log.i(TAG, "media $id: ${e.message}")
            null
        }
    }

    fun thumbnail(uri: Uri): Bitmap? = try {
        context.contentResolver.loadThumbnail(uri, Size(640, 480), null)   // big enough for the large picture
    } catch (e: Exception) {
        null
    }

    /** a thumbnail (a play sign on a video) as the preview, under its "+N more" badge */
    fun show(preview: FrameLayout, video: Boolean, thumb: Bitmap?) {
        val badge = preview.findViewById<android.view.View>(R.id.leak_more)
        preview.removeAllViews()
        preview.addView(ImageView(context).apply {
            scaleType = ImageView.ScaleType.CENTER_CROP
            setImageBitmap(thumb)
        }, FrameLayout.LayoutParams(-1, -1))
        if (video)
            preview.addView(ImageView(context).apply { setImageResource(android.R.drawable.ic_media_play) },
                            FrameLayout.LayoutParams(-2, -2, android.view.Gravity.CENTER))
        preview.addView(badge)
    }

    /** one picture or video alone in the phone's viewer or player (user 2026-11-07: "better open file alone") */
    fun view(uri: Uri, video: Boolean) = context.startActivity(
        Intent(Intent.ACTION_VIEW).setDataAndType(uri, if (video) "video/mp4" else "image/jpeg")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION))

    /** the phone's gallery on its newest pictures and videos: this leak's record at the top. No gallery opens one
     *  album by a standard intent, and a single file opens alone (Samsung: no swipe to the others, 2026-11-07) */
    fun open() = context.startActivity(
        Intent(Intent.ACTION_VIEW).setDataAndType(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, "vnd.android.cursor.dir/image"))

    companion object {
        const val TAG = "LeakcamGallery"
        const val RETRY_MS = 5_000L     // while the panel is open only: no polling in the background
        const val CAMS = 2              // the board's cameras (server Cameras): picture n is camera (n-1) % CAMS
    }
}
