package dev.itzkaguya.aospcontainer.ui.install

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import dev.itzkaguya.aospcontainer.core.ExtractionState
import dev.itzkaguya.aospcontainer.core.RootFsExtractor
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * RootFS installation flow: SAF document picker -> streaming .tar.xz
 * extraction with live progress emitted by [RootFsExtractor].
 */
@Composable
fun InstallScreen(
    modifier: Modifier = Modifier,
    onLaunchContainer: (() -> Unit)? = null,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var extracting by remember { mutableStateOf(false) }
    var currentFile by remember { mutableStateOf("") }
    var percentage by remember { mutableStateOf(-1) }
    var resultMessage by remember { mutableStateOf<String?>(null) }
    var installed by remember { mutableStateOf(false) }

    val picker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        extracting = true
        installed = false
        resultMessage = null
        scope.launch {
            try {
                val outcome = withContext(Dispatchers.IO) {
                    val stream = context.contentResolver.openInputStream(uri)
                        ?: throw IllegalStateException("cannot open selected file")
                    val collector = RootFsExtractor(context)
                    var completed: ExtractionState.Completed? = null
                    stream.use { input ->
                        collector.extractRootFs(input).collect { state ->
                            when (state) {
                                is ExtractionState.Progress -> {
                                    percentage = state.percentage
                                    currentFile = state.currentFile
                                }
                                is ExtractionState.Completed -> completed = state
                                is ExtractionState.Error -> throw state.throwable
                            }
                        }
                    }
                    completed
                }
                resultMessage = outcome?.let {
                    "Installed '${it.manifest.name}' v${it.manifest.version} " +
                        "(${it.manifest.arch}, API ${it.manifest.androidApi})"
                } ?: "Extraction finished"
            } catch (t: Throwable) {
                resultMessage = "Install failed: ${t.message}"
            } finally {
                extracting = false
            }
        }
    }

    Column(
        modifier = modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterVertically),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("Install container ROM", style = MaterialTheme.typography.headlineSmall)
        Text(
            "Select a .tar.xz rootfs archive containing manifest.json",
            style = MaterialTheme.typography.bodyMedium,
        )

        Button(
            onClick = { picker.launch(arrayOf("application/x-xz", "*/*")) },
            enabled = !extracting,
        ) {
            Text(if (extracting) "Extracting…" else "Choose archive")
        }

        if (installed && onLaunchContainer != null) {
            Button(onClick = onLaunchContainer) {
                Text("Launch container")
            }
        }

        if (extracting) {
            if (percentage >= 0) {
                LinearProgressIndicator(
                    progress = { percentage / 100f },
                    modifier = Modifier.fillMaxWidth(),
                )
            } else {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }
            if (currentFile.isNotEmpty()) {
                Text(currentFile, style = MaterialTheme.typography.bodySmall, maxLines = 1)
            }
        }

        resultMessage?.let {
            Text(
                it,
                style = MaterialTheme.typography.bodyMedium,
                color = if (it.startsWith("Install failed")) {
                    MaterialTheme.colorScheme.error
                } else {
                    MaterialTheme.colorScheme.primary
                },
            )
        }
    }
}
