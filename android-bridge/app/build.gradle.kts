plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

/* Tek sürüm kaynağı: depo kökündeki version.txt (firmware ile ortak).
 * versionCode = major*10000 + minor*100 + patch. */
val projectVersion = rootDir.resolve("../version.txt").readText().trim()
val projectVersionCode = projectVersion.split(".").map { it.toInt() }
    .let { (ma, mi, pa) -> ma * 10000 + mi * 100 + pa }

android {
    namespace = "com.obdnav.bridge"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.obdnav.bridge"
        minSdk = 26
        targetSdk = 34
        versionCode = projectVersionCode
        versionName = projectVersion
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
