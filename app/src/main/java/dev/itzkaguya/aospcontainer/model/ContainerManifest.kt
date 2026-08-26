package dev.itzkaguya.aospcontainer.model

import com.google.gson.annotations.SerializedName

data class ContainerManifest(
    @SerializedName("name") val name: String,
    @SerializedName("flavor") val flavor: String? = null,
    @SerializedName("version") val version: String,
    @SerializedName("arch") val arch: String,
    @SerializedName("android_api") val androidApi: Int,
    @SerializedName("min_app_version") val minAppVersion: Int,
    @SerializedName("display") val display: DisplayConfig? = null,
    @SerializedName("default_env") val defaultEnv: Map<String, String>? = null
)

data class DisplayConfig(
    @SerializedName("default_density") val defaultDensity: Int = 420,
    @SerializedName("default_fps") val defaultFps: Int = 60
)
