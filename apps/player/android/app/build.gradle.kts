plugins {
    id("com.android.application")
}

android {
    namespace = "com.devex.player"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.devex.player"
        // Android 10, as ANDROID_PLATFORM in the android-arm64 preset.
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        // Phones, and the emulator on a PC: the APK holds the ABIs that were built.
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    // The package of the game is mapped where it is stored in the APK, so it must not be compressed.
    androidResources {
        noCompress += "dvxpak"
    }
}
