package dev.itzkaguya.aospcontainer.ui

import android.content.Context
import org.json.JSONObject
import java.io.File

/** Persisted per-instance container configuration. */
data class InstanceConfig(
    val romId: String = "",
    val romName: String = "",
    val displayWidth: Int = 720,
    val displayHeight: Int = 1280,
    val dpi: Int = 240,
) {
    val isImported: Boolean get() = romId.isNotEmpty()

    fun toJson(): String = JSONObject().apply {
        put("romId", romId)
        put("romName", romName)
        put("displayWidth", displayWidth)
        put("displayHeight", displayHeight)
        put("dpi", dpi)
    }.toString()

    companion object {
        private const val FILE = "instance.json"

        fun load(context: Context): InstanceConfig {
            val f = File(context.filesDir, FILE)
            if (!f.exists()) return InstanceConfig()
            return runCatching {
                val o = JSONObject(f.readText())
                InstanceConfig(
                    romId = o.optString("romId"),
                    romName = o.optString("romName"),
                    displayWidth = o.optInt("displayWidth", 720),
                    displayHeight = o.optInt("displayHeight", 1280),
                    dpi = o.optInt("dpi", 240),
                )
            }.getOrDefault(InstanceConfig())
        }

        fun save(context: Context, config: InstanceConfig) {
            File(context.filesDir, FILE).writeText(config.toJson())
        }
    }
}
