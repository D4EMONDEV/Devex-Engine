// The Android project of the Devex player. The build of the android-arm64 preset copies it into
// out/build/android-arm64/android with the libraries and the files of the engine.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "DevexPlayer"
include(":app")
