package com.leakcam.setup

import android.os.Bundle
import android.util.Base64
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.AndroidJUnitRunner
import androidx.test.uiautomator.Configurator
import androidx.test.uiautomator.UiDevice
import org.junit.Test
import org.junit.runner.RunWith
import java.io.ByteArrayOutputStream

/** the runner of Screen: it instruments the test APK, so the app under test keeps running (AndroidManifest.xml) */
class SelfRunner : AndroidJUnitRunner()

/**
 * The screen's view tree for the test scripts (tools/test-runner/phone.py screen_xml()): `uiautomator dump` waits for
 * 1 s of idle screen up to 10 s, and the setup screen's spinner never idles, so every dump there took 10 s (test135,
 * 2026-11-08); this one waits at most 300 ms. The XML comes back base64 in the status bundle (`xml=`).
 *   am instrument -w -e class com.leakcam.setup.Screen com.leakcam.setup.test/com.leakcam.setup.SelfRunner
 */
@RunWith(AndroidJUnit4::class)
class Screen {
    @Test
    fun dump() {
        Configurator.getInstance().waitForIdleTimeout = 300
        val out = ByteArrayOutputStream()
        UiDevice.getInstance(InstrumentationRegistry.getInstrumentation()).dumpWindowHierarchy(out)
        InstrumentationRegistry.getInstrumentation().sendStatus(0, Bundle().apply {
            putString("xml", Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP))
        })
    }
}
