plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.horizonos.swm"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.horizonos.swm"
        minSdk = 28
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
		missingDimensionStrategy("products", "editor")
    }

	signingConfigs {
		getByName("debug") {
			storeFile = file("../../../../../../GodotSWM/keys/debug.keystore")
			storePassword = "android"
			keyAlias = "android"
			keyPassword = "android"
		}
	}

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_21
		targetCompatibility = JavaVersion.VERSION_21
	}
	kotlinOptions {
		jvmTarget = "21"
	}
}

dependencies {
	// GodotSWM stuff, excluding a prebuilt editor, replaced by direct dependency on the engine src
	implementation(files("../swm-libs/hzos.jar"))
	implementation(files("../swm-libs/vrosx-internal.aar"))
	implementation(files("../swm-libs/godotopenxr-meta-debug.aar"))
	implementation(project(":lib"))

	// Standard Android module stuff
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
	testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.6.1")
}
