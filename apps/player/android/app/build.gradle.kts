import java.util.Properties

plugins {
    id("com.android.application")
}

// What the export knows of the game: without this file, the project still builds the player.
val game = Properties().apply {
    val file = rootProject.file("devex.properties")
    if (file.exists()) {
        file.reader(Charsets.UTF_8).use { load(it) }
    }
}

android {
    namespace = "com.devex.player"
    compileSdk = 35

    defaultConfig {
        applicationId = game.getProperty("applicationId", "com.devex.player")
        // Android 10, as ANDROID_PLATFORM in the android-arm64 preset.
        minSdk = 29
        targetSdk = 35
        versionCode = game.getProperty("versionCode", "1").toInt()
        versionName = game.getProperty("versionName", "0.1.0")
        manifestPlaceholders["gameName"] = game.getProperty("gameName", "Devex Player")
        manifestPlaceholders["orientation"] = game.getProperty("orientation", "sensorLandscape")
        manifestPlaceholders["icon"] = game.getProperty("icon", "@android:drawable/sym_def_app_icon")
        // Phones, and the emulator on a PC: the APK holds the ABIs that were built.
        ndk {
            abiFilters += game.getProperty("abis", "arm64-v8a,x86_64").split(",")
        }
    }

    // The key of a release, which the debug builds do without.
    signingConfigs {
        game.getProperty("keystore")?.let { keystore ->
            create("game") {
                storeFile = file(keystore)
                storePassword = game.getProperty("storePassword")
                keyAlias = game.getProperty("keyAlias")
                keyPassword = game.getProperty("keyPassword")
            }
        }
    }
    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("game")
        }
    }

    // The package of the game is mapped where it is stored in the APK, so it must not be compressed.
    androidResources {
        noCompress += "dvxpak"
    }

    // On disk once installed: the engine looks for the module of the game beside its own library.
    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}
