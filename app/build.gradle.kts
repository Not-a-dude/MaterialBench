plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.komarudude.materialbench"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.komarudude.materialbench"
        minSdk = 30
        targetSdk = 36
        versionCode = 18
        versionName = "1.2.0-beta5"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndkVersion = "29.0.14206865"

        ndk {
            abiFilters.add("arm64-v8a")
            abiFilters.add("x86_64")
        }

        val compVersion = System.getenv("COMPANION_MIN_VERSION") ?: "11"
        val compHash = System.getenv("COMPANION_SHA256") ?: "429f6c1e57e8b9b7474c91d5109fcc5e66f78211c7b2bf14bdf4fb7999c93cbd"

        buildConfigField("long", "COMPANION_MIN_VERSION", "${compVersion}L")
        buildConfigField("String", "COMPANION_SHA256", "\"$compHash\"")
    }

    signingConfigs {
        create("release") {
            storeFile = file(project.property("KEYSTORE_FILE_PATH").toString())
            storePassword = project.property("KEY_PASSWORD").toString()
            keyAlias = project.property("KEY_ALIAS").toString()
            keyPassword = project.property("KEYSTORE_PASSWORD").toString()
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            signingConfig = signingConfigs.getByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    kotlin {
        android {
            compilerOptions {
                jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_11)
            }
        }
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
        }
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.androidx.material3)
    implementation(libs.androidx.compose.material3.android)
    implementation(libs.androidx.compose.material.icons.core)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.ui.tooling)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.material3.adaptive.navigation.suite)
    implementation(libs.androidx.core.splash.screen) // На будущее, сейчас я не знаю как реализовать это
    implementation(libs.litert)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.ui.test.junit4)
    debugImplementation(libs.androidx.ui.tooling)
    debugImplementation(libs.androidx.ui.test.manifest)
    debugImplementation(libs.androidx.compose.ui.tooling)
    implementation(libs.retrofit.core)
    implementation(libs.retrofit.converter.gson)
    implementation(libs.vico.core)
    implementation(libs.vico.compose)
    implementation(libs.vico.compose.m3)
    implementation(libs.androidx.appcompat)
}
