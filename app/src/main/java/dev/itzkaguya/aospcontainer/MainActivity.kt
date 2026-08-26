package dev.itzkaguya.aospcontainer

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import dev.itzkaguya.aospcontainer.ui.install.InstallScreen
import dev.itzkaguya.aospcontainer.ui.viewport.ContainerViewportScreen
import java.io.File

enum class AppDestination {
    HOME,
    VIEWPORT
}

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    // Always default to HOME screen on fresh activity launch
                    var currentDestination by remember { mutableStateOf(AppDestination.HOME) }
                    val rootfsDir = remember { File(filesDir, "rootfs") }

                    when (currentDestination) {
                        AppDestination.HOME -> {
                            InstallScreen(
                                onLaunchContainer = {
                                    currentDestination = AppDestination.VIEWPORT
                                }
                            )
                        }
                        AppDestination.VIEWPORT -> {
                            ContainerViewportScreen(
                                rootfsPath = rootfsDir.absolutePath,
                                initBinary = "/system/bin/sh",
                                onExitContainer = {
                                    currentDestination = AppDestination.HOME
                                }
                            )
                        }
                    }
                }
            }
        }
    }
}
