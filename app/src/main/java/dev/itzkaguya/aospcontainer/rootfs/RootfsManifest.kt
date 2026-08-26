package dev.itzkaguya.aospcontainer.rootfs

import org.json.JSONException
import org.json.JSONObject

/**
 * manifest.json contract for container rootfs archives.
 *
 * ```json
 * {
 *   "id": "lineage-gsi",
 *   "name": "LineageOS GSI (container build)",
 *   "arch": "arm64",
 *   "androidVersion": "13",
 *   "initPath": "/init",
 *   "patchInterp": true,
 *   "displayWidth": 720,
 *   "displayHeight": 1280,
 *   "dpi": 240
 * }
 * ```
 */
data class RootfsManifest(
    val id: String,
    val name: String,
    val arch: String,
    val androidVersion: String,
    val initPath: String,
    val patchInterp: Boolean,
    val displayWidth: Int,
    val displayHeight: Int,
    val dpi: Int,
) {
    companion object {
        const val ARCH_ARM64 = "arm64"
        private const val DEFAULT_INIT = "/init"

        fun parse(json: String): RootfsManifest {
            val obj = try {
                JSONObject(json)
            } catch (e: JSONException) {
                throw ImportException("manifest.json is not valid JSON", e)
            }
            return RootfsManifest(
                id = obj.optString("id").ifBlank { throw ImportException("manifest.id missing") },
                name = obj.optString("name").ifBlank { obj.getString("id") },
                arch = obj.optString("arch", RootfsManifest.Companion.ARCH_ARM64),
                androidVersion = obj.optString("androidVersion", ""),
                initPath = obj.optString("initPath", DEFAULT_INIT).let {
                    require(it.startsWith("/")) { throw ImportException("manifest.initPath must be absolute") }
                    it
                },
                patchInterp = obj.optBoolean("patchInterp", true),
                displayWidth = obj.optInt("displayWidth", 720).coerceIn(240, 2160),
                displayHeight = obj.optInt("displayHeight", 1280).coerceIn(240, 2160),
                dpi = obj.optInt("dpi", 240).coerceIn(120, 640),
            )
        }
    }
}

class ImportException(message: String, cause: Throwable? = null) : Exception(message, cause)
