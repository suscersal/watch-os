plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.watchos.sync"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.watchos.sync"
        minSdk = 26
        targetSdk = 34
        versionCode = System.getenv("GITHUB_RUN_NUMBER")?.toIntOrNull() ?: 1   // растёт с каждой сборкой в Actions
        versionName = "0.1"
    }
    // Один и тот же ключ для всех сборок: иначе у каждой сборки в Actions своя подпись, и Android не ставит её поверх прежней
    signingConfigs {
        getByName("debug") {
            storeFile = rootProject.file("../signing/watch-debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }
    buildTypes {
        release { isMinifyEnabled = false }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
