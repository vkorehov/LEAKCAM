package com.leakcam.setup

import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiDevice
import org.junit.Test
import org.junit.runner.RunWith

/**
 * LEAKCAM Focus Adjustment driven as a person does (test code only; tests/test146 app_mock.py, DESIGN.md 5.3.17): the
 * screen opened for a board as the status screen's Tune focus opens it, then the Camera 2 / Camera 1 buttons tapped on
 * the screen at the given seconds, then the screen left (its Stop). The board is the server's: a real one, or
 * tools/test-runner/mock_board_focus.py against a local server (the app built with -PleakcamServer=...).
 *   adb shell am instrument -w -e class com.leakcam.setup.FocusScreen -e id <device> [-e taps 8:2,14:1] [-e secs 20] \
 *       com.leakcam.setup.test/androidx.test.runner.AndroidJUnitRunner
 * taps: <second>:<camera 1|2>, comma-separated, from the screen's opening.
 */
@RunWith(AndroidJUnit4::class)
class FocusScreen {
    @Test
    fun run() {
        val inst = InstrumentationRegistry.getInstrumentation()
        val args = InstrumentationRegistry.getArguments()
        val id = args.getString("id") ?: "7C3E8262B149"
        val secs = args.getString("secs")?.toIntOrNull() ?: 20
        val taps = (args.getString("taps") ?: "8:2,14:1").split(",").mapNotNull {
            val p = it.split(":")
            p.getOrNull(0)?.toIntOrNull()?.let { s -> s to (p.getOrNull(1)?.toIntOrNull() ?: 1) }
        }.sortedBy { it.first }
        val ctx = inst.targetContext
        val ui = UiDevice.getInstance(inst)
        inst.startActivitySync(Intent(ctx, FocusActivity::class.java).putExtra(FocusActivity.EXTRA_ID, id)
            .putExtra(FocusActivity.EXTRA_CAM, 0).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        val t0 = System.currentTimeMillis()
        for ((at, camera) in taps) {
            val wait = t0 + at * 1000L - System.currentTimeMillis()
            if (wait > 0) Thread.sleep(wait)
            val label = Cams.name(ctx, camera - 1)              // camera 1 = the bottom one, 2 = the top one
            val b = ui.findObject(By.textContains(label.uppercase())) ?: ui.findObject(By.textContains(label))
            requireNotNull(b) { "no button $label" }.click()
            android.util.Log.i("LeakcamFocusTest", "tapped Camera $camera ($label) at ${System.currentTimeMillis() - t0} ms")
        }
        val left = t0 + secs * 1000L - System.currentTimeMillis()
        if (left > 0) Thread.sleep(left)
        ui.pressBack()                                          // the screen left: its onStop sends Stop
        Thread.sleep(1000)
    }
}
