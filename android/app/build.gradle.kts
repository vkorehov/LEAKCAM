plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("com.google.gms.google-services")
}

android {
    namespace = "com.leakcam.setup"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.leakcam.setup"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"   // BleOp: tests/test040, test106
        // the server (the wired PC since 2026-11-12); -PleakcamServer=http://127.0.0.1:8090 for a test build against a
        // local server through adb reverse (tools/test-runner/mock_board_video.py, tests/test146)
        buildConfigField("String", "SERVER", "\"${findProperty("leakcamServer") ?: "http://192.168.1.131:8090"}\"")
    }
    buildFeatures {
        buildConfig = true
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    // the server's push for the leak alert (DESIGN.md 5.3.13): Firebase Cloud Messaging, the app's only library
    implementation(platform("com.google.firebase:firebase-bom:33.7.0"))
    implementation("com.google.firebase:firebase-messaging")
    // host unit tests (src/test: FocusGuideTest)
    testImplementation("junit:junit:4.13.2")
    // the BLE test driver (src/androidTest/.../BleOp.kt) only
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test.uiautomator:uiautomator:2.3.0")
}
