package com.leakcam.setup

import android.app.Activity
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.VideoView
import java.net.HttpURLConnection
import java.net.URL

/**
 * The leak panel's gallery (DESIGN.md 5.3.13): each picture and video by the ID the push announced, from the server
 * (GET /v1/media/<id>); its tile keeps its spinner until the file is there (the board uploads it after the push), asked
 * again every few seconds while the panel is open. A picture fills its tile; a video plays in its tile on a tap.
 */
class Gallery(private val activity: Activity) {
    private val main = Handler(Looper.getMainLooper())
    private val done = mutableSetOf<String>()

    fun fetch(id: String, tile: FrameLayout) {
        if (id in done)
            return
        val video = id.substringAfterLast('-').startsWith("v")
        val url = "${BuildConfig.SERVER}/v1/media/$id"
        Thread {
            try {
                val c = URL(url).openConnection() as HttpURLConnection
                c.connectTimeout = 5000
                c.readTimeout = 15000
                if (video) c.requestMethod = "HEAD"
                if (c.responseCode == 200) {
                    val bmp = if (video) null else c.inputStream.use { BitmapFactory.decodeStream(it) }
                    c.disconnect()
                    main.post { show(id, tile, video, url, bmp) }
                    return@Thread
                }
                c.disconnect()
            } catch (e: Exception) {
                Log.i(TAG, "media $id: ${e.message}")
            }
            // not there yet: asked again while the tile is on the screen
            main.postDelayed({ if (tile.isAttachedToWindow) fetch(id, tile) }, RETRY_MS)
        }.start()
    }

    private fun show(id: String, tile: FrameLayout, video: Boolean, url: String, bmp: android.graphics.Bitmap?) {
        done.add(id)
        tile.removeAllViews()
        if (!video) {
            tile.addView(ImageView(activity).apply {
                scaleType = ImageView.ScaleType.CENTER_CROP
                setImageBitmap(bmp)
                contentDescription = id
            }, FrameLayout.LayoutParams(-1, -1))
        } else {
            // its first frame with a play sign on it (a bare VideoView is black until it plays); the sign goes on play
            val v = VideoView(activity).apply {
                setVideoURI(Uri.parse(url))
                setOnPreparedListener { seekTo(1) }
            }
            val play = ImageView(activity).apply { setImageResource(android.R.drawable.ic_media_play) }
            tile.addView(v, FrameLayout.LayoutParams(-1, -1, android.view.Gravity.CENTER))
            tile.addView(play, FrameLayout.LayoutParams(-2, -2, android.view.Gravity.CENTER))
            tile.contentDescription = activity.getString(R.string.play_video)
            tile.setOnClickListener {
                if (v.isPlaying) v.pause() else v.start()
                play.visibility = if (v.isPlaying) android.view.View.GONE else android.view.View.VISIBLE
            }
            v.setOnCompletionListener { play.visibility = android.view.View.VISIBLE }
        }
        Log.i(TAG, "media $id shown")
    }

    companion object {
        const val TAG = "LeakcamGallery"
        const val RETRY_MS = 5_000L     // while the panel is open only: no polling in the background
    }
}
