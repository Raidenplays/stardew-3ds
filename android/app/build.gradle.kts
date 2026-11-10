// stardew3ds converter for Android: runs the same tools/convert.py as the PC,
// through Chaquopy, on game files you pick from your phone's storage.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("com.chaquo.python")
}

android {
    namespace = "net.itsraiden.stardew3ds.converter"
    compileSdk = 34
    defaultConfig {
        applicationId = "net.itsraiden.stardew3ds.converter"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += listOf("arm64-v8a") }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")   // personal use: debug key
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

chaquopy {
    defaultConfig {
        version = "3.11"
        pip {
            install("pillow")
            install("numpy")
        }
    }
    sourceSets {
        getByName("main") {
            srcDir("../../tools")   // the very same converter as on the PC
        }
    }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("commons-net:commons-net:3.11.1")   // FTP to the 3DS (ftpd)
}
