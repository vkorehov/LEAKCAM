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
        // the presence server (server/, the Pi's systemd unit leakcam-server)
        buildConfigField("String", "SERVER", "\"http://192.168.1.159:8090\"")
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
    // the BLE test driver (src/androidTest/.../BleOp.kt) only
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test.uiautomator:uiautomator:2.3.0")
}
