package dev.itzkaguya.aospcontainer.ui

import android.content.Context
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import dev.itzkaguya.aospcontainer.core.ContainerCore
import dev.itzkaguya.aospcontainer.input.ContainerInput
import dev.itzkaguya.aospcontainer.rootfs.RootfsImporter
import dev.itzkaguya.aospcontainer.service.ContainerService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * Instance management: import ROM, configure display, start/stop the
 * container, back up or reset /data, and view the live guest screen.
 */
@Composable
fun ContainerManagerScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    var config by remember { mutableStateOf(InstanceConfig.load(context)) }
    var status by remember { mutableStateOf("") }
    var running by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    /* Poll native container state for live UI reflection. */
    LaunchedEffect(Unit) {
        while (true) {
            running = ContainerCore.nativeGetState() == ContainerCore.STATE_RUNNING
            delay(1000)
        }
    }

    val romPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            scope.launch {
                status = "Importing…"
                try {
                    withContext(Dispatchers.IO) {
                        context.contentResolver.openInputStream(uri)!!.use { input ->
                            RootfsImporter.import(context, input)
                        }
                    }.let { m ->
                        config = config.copy(romId = m.id, romName = m.name,
                                             displayWidth = m.displayWidth,
                                             displayHeight = m.displayHeight, dpi = m.dpi)
                        InstanceConfig.save(context, config)
                        status = "Imported: ${m.name}"
                    }
                } catch (e: Exception) {
                    status = "Import failed: ${e.message}"
                }
            }
        }
    }

    Column(
        modifier = modifier.fillMaxSize().padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("AOSP Container", style = MaterialTheme.typography.headlineSmall)
        Text("Engine ${ContainerCore.nativeVersion()} · ROM: ${
            config.romName.ifEmpty { "(none imported)" }
        }", style = MaterialTheme.typography.bodySmall)

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(onClick = {
                romPicker.launch(arrayOf("application/x-xz", "application/zip", "*/*"))
            }) { Text("Import ROM") }

            if (config.isImported) {
                Button(onClick = {
                    ContainerInput.setViewport(config.displayWidth, config.displayHeight)
                    ContextCompat.startForegroundService(
                        context,
                        android.content.Intent(context, ContainerService::class.java),
                    )
                    ContainerService.start(context)
                    status = "Starting…"
                }) { Text(if (running) "Restart" else "Start") }

                OutlinedButton(onClick = {
                    ContainerService.stop(context)
                    status = "Stopped"
                }) { Text("Stop") }
            }
        }

        if (config.isImported && !running) {
            DisplaySettingsRow(config) { updated ->
                config = updated
                InstanceConfig.save(context, updated)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton(onClick = {
                    scope.launch {
                        status = withContext(Dispatchers.IO) {
                            backupData(context, config)
                        }
                    }
                }) { Text("Backup /data") }

                OutlinedButton(onClick = {
                    scope.launch {
                        status = withContext(Dispatchers.IO) {
                            resetData(context, config); "/data reset"
                        }
                    }
                }) { Text("Reset /data") }
            }
        }

        if (running && config.isImported) {
            Text("Live guest display:", style = MaterialTheme.typography.titleSmall)
            ContainerViewport(aspectRatio = config.displayWidth.toFloat() / config.displayHeight)
        }

        if (status.isNotEmpty()) {
            Text(status, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
private fun DisplaySettingsRow(
    config: InstanceConfig,
    onUpdate: (InstanceConfig) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    val resolutions = listOf("720x1280", "1080x1920", "1440x2560")
    val current = "${config.displayWidth}x${config.displayHeight}"

    ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = it }) {
        OutlinedTextField(
            value = current,
            onValueChange = {},
            readOnly = true,
            label = { Text("Guest resolution") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier.menuAnchor().fillMaxWidth(),
        )
        ExposedDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
        ) {
            resolutions.forEach { r ->
                DropdownMenuItem(
                    text = { Text(r) },
                    onClick = {
                        val (w, h) = r.split('x').map { it.toInt() }
                        onUpdate(config.copy(displayWidth = w, displayHeight = h))
                        expanded = false
                    },
                )
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* data management                                                     */
/* ------------------------------------------------------------------ */

private fun instanceRoot(context: Context, config: InstanceConfig): File =
    File(context.filesDir, "rootfs/${config.romId}")

private fun resetData(context: Context, config: InstanceConfig): String {
    val dataDir = File(instanceRoot(context, config), "data")
    if (dataDir.exists()) dataDir.deleteRecursively()
    dataDir.mkdirs()
    return "/data reset"
}

private fun backupData(context: Context, config: InstanceConfig): String {
    val dataDir = File(instanceRoot(context, config), "data")
    if (!dataDir.exists()) return "nothing to back up"

    val backups = File(context.filesDir, "backups").apply { mkdirs() }
    val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
    val out = File(backups, "data-$stamp.zip")

    java.io.FileOutputStream(out).use { fos ->
        ZipOutputStream(fos).use { zip ->
        dataDir.walkTopDown().filter { it.isFile }.forEach { f ->
            val rel = f.relativeTo(dataDir).path
            zip.putNextEntry(ZipEntry(rel))
                f.inputStream().use { it.copyTo(zip) }
                zip.closeEntry()
            }
        }
    }
    return "backup -> ${out.name} (${out.length() / 1024} KiB)"
}

