package dev.itzkaguya.aospcontainer.model

import com.google.gson.annotations.SerializedName

data class ContainerManifest(
    @SerializedName("id") val id: String = "aosp_container_guest",
    @SerializedName("name") val name: String = "AOSP Container",
    @SerializedName("flavor") val flavor: String? = null,
    @SerializedName("version") val version: String = "1.0.0",
    @SerializedName("arch") val arch: String = "arm64-v8a",
    @SerializedName("android_api") val androidApi: Int = 34,
    @SerializedName("min_app_version") val minAppVersion: Int = 1,
    @SerializedName("display") val display: DisplayConfig? = null,
    @SerializedName("default_env") val defaultEnv: Map<String, String>? = null
)

data class DisplayConfig(
    @SerializedName("default_density") val defaultDensity: Int = 420,
    @SerializedName("default_fps") val defaultFps: Int = 60
)
