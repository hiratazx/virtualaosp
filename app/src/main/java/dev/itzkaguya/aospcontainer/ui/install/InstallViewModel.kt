package dev.itzkaguya.aospcontainer.ui.install

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.gson.Gson
import dev.itzkaguya.aospcontainer.core.ExtractionState
import dev.itzkaguya.aospcontainer.core.RootFsExtractor
import dev.itzkaguya.aospcontainer.model.ContainerManifest
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.File

sealed interface RomUiState {
    data object NotInstalled : RomUiState
    data class Extracting(val currentFile: String) : RomUiState
    data class Ready(val manifest: ContainerManifest) : RomUiState
    data class Error(val message: String) : RomUiState
}

class InstallViewModel(application: Application) : AndroidViewModel(application) {

    private val extractor = RootFsExtractor(application)
    private val gson = Gson()
    private val rootfsDir = File(application.filesDir, "rootfs")
    private val manifestFile = File(rootfsDir, "manifest.json")

    private val _uiState = MutableStateFlow<RomUiState>(RomUiState.NotInstalled)
    val uiState: StateFlow<RomUiState> = _uiState.asStateFlow()

    init {
        checkInstalledRom()
    }

    fun checkInstalledRom() {
        viewModelScope.launch(Dispatchers.IO) {
            if (manifestFile.exists() && rootfsDir.exists()) {
                try {
                    val manifest = gson.fromJson(manifestFile.readText(), ContainerManifest::class.java)
                    _uiState.value = RomUiState.Ready(manifest)
                } catch (e: Exception) {
                    _uiState.value = RomUiState.NotInstalled
                }
            } else {
                _uiState.value = RomUiState.NotInstalled
            }
        }
    }

    fun startInstallation(archiveUri: Uri) {
        viewModelScope.launch {
            val contentResolver = getApplication<Application>().contentResolver
            val inputStream = contentResolver.openInputStream(archiveUri)

            if (inputStream == null) {
                _uiState.value = RomUiState.Error("Unable to open archive stream.")
                return@launch
            }

            extractor.extractRootFs(inputStream, rootfsDir).collect { state ->
                when (state) {
                    is ExtractionState.Progress -> {
                        _uiState.value = RomUiState.Extracting(state.currentFile)
                    }
                    is ExtractionState.Completed -> {
                        _uiState.value = RomUiState.Ready(state.manifest)
                    }
                    is ExtractionState.Error -> {
                        _uiState.value = RomUiState.Error(
                            state.throwable.localizedMessage ?: "Extraction failed."
                        )
                    }
                }
            }
        }
    }

    fun resetToInstall() {
        _uiState.value = RomUiState.NotInstalled
    }
}
