package com.leakcam.setup

import android.content.Context

/**
 * The two cameras' names in the product (user 2026-11-13: "don't use camera1 and camera2 in product, use top camera
 * and bottom camera"): camera 0 (J4) looks down, camera 1 (J5) up (DESIGN.md 2). The numbers stay in the protocol
 * (cam 0 / 1) and the logs.
 */
object Cams {
    /** at the start of a sentence or alone: "Top camera" */
    fun name(c: Context, cam: Int): String = c.getString(if (cam == 0) R.string.camera_0 else R.string.camera_1)

    /** inside a sentence: "top camera" */
    fun inText(c: Context, cam: Int): String = c.getString(if (cam == 0) R.string.camera_0_in else R.string.camera_1_in)
}
