plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.obdnav.bridge"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.obdnav.bridge"
        minSdk = 26
        targetSdk = 34
        versionCode = 8
        versionName = "0.8"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            /* Elle yükleme (sideload) için debug anahtarıyla imzalı. */
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}
