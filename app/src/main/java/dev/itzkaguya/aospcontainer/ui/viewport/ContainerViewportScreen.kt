package dev.itzkaguya.aospcontainer.ui.viewport

import android.annotation.SuppressLint
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import android.view.KeyEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import dev.itzkaguya.aospcontainer.core.ContainerNativeBridge
import dev.itzkaguya.aospcontainer.core.NativeContainerState
import dev.itzkaguya.aospcontainer.service.ContainerService

@SuppressLint("ClickableViewAccessibility")
@Composable
fun ContainerViewportScreen(
    onExitContainer: () -> Unit,
    modifier: Modifier = Modifier,
    rootfsPath: String? = null,
    initBinary: String = "/system/bin/sh"
) {
    val context = LocalContext.current
    var containerService by remember { mutableStateOf<ContainerService?>(null) }
    var isBound by remember { mutableStateOf(false) }
    var showControls by remember { mutableStateOf(false) }

    val serviceConnection = remember {
        object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
                val containerBinder = binder as? ContainerService.ContainerBinder
                containerService = containerBinder?.getService()
                isBound = true
            }

            override fun onServiceDisconnected(name: ComponentName?) {
                containerService = null
                isBound = false
            }
        }
    }

    DisposableEffect(Unit) {
        ContainerService.start(context, rootfsPath, initBinary)
        val bindIntent = Intent(context, ContainerService::class.java)
        context.bindService(bindIntent, serviceConnection, Context.BIND_AUTO_CREATE)

        onDispose {
            if (isBound) {
                context.unbindService(serviceConnection)
                isBound = false
            }
        }
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(Color.Black)
    ) {
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { ctx ->
                SurfaceView(ctx).apply {
                    setZOrderOnTop(false)
                    setOnTouchListener(ContainerTouchHandler())

                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) {
                            ContainerNativeBridge.nativeOnSurfaceCreated(holder.surface)
                        }

                        override fun surfaceChanged(
                            holder: SurfaceHolder,
                            format: Int,
                            width: Int,
                            height: Int
                        ) {
                            // Handles dynamic orientation changes, multi-window split, and tablet rotation
                            ContainerNativeBridge.nativeOnSurfaceChanged(holder.surface, width, height)
                        }

                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                            ContainerNativeBridge.nativeOnSurfaceDestroyed()
                        }
                    })
                }
            }
        )

        IconButton(
            onClick = { showControls = !showControls },
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(16.dp)
                .background(
                    color = Color.Black.copy(alpha = 0.5f),
                    shape = RoundedCornerShape(50)
                )
        ) {
            Icon(
                imageVector = if (showControls) Icons.Default.Close else Icons.Default.Tune,
                contentDescription = "Toggle Controls",
                tint = Color.White
            )
        }

        AnimatedVisibility(
            visible = showControls,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier.align(Alignment.BottomCenter)
        ) {
            ContainerControlBar(
                onBack = { sendKey(KeyEvent.KEYCODE_BACK) },
                onHome = { sendKey(KeyEvent.KEYCODE_HOME) },
                onRecents = { sendKey(KeyEvent.KEYCODE_APP_SWITCH) },
                onVolumeDown = { sendKey(KeyEvent.KEYCODE_VOLUME_DOWN) },
                onVolumeUp = { sendKey(KeyEvent.KEYCODE_VOLUME_UP) },
                onPower = { sendKey(KeyEvent.KEYCODE_POWER) },
                onCloseContainer = {
                    ContainerService.stop(context)
                    onExitContainer()
                }
            )
        }
    }
}

@Composable
private fun ContainerControlBar(
    onBack: () -> Unit,
    onHome: () -> Unit,
    onRecents: () -> Unit,
    onVolumeDown: () -> Unit,
    onVolumeUp: () -> Unit,
    onPower: () -> Unit,
    onCloseContainer: () -> Unit
) {
    Surface(
        color = MaterialTheme.colorScheme.surfaceColorAtElevation(6.dp).copy(alpha = 0.92f),
        shape = RoundedCornerShape(28.dp),
        modifier = Modifier
            .padding(16.dp)
            .navigationBarsPadding()
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            ControlIconButton(icon = Icons.Default.ArrowBack, contentDesc = "Back", onClick = onBack)
            ControlIconButton(icon = Icons.Default.Circle, contentDesc = "Home", onClick = onHome)
            ControlIconButton(icon = Icons.Default.CropSquare, contentDesc = "Recents", onClick = onRecents)

            VerticalDivider(modifier = Modifier.height(24.dp).padding(horizontal = 4.dp))

            ControlIconButton(icon = Icons.Default.VolumeDown, contentDesc = "Vol -", onClick = onVolumeDown)
            ControlIconButton(icon = Icons.Default.VolumeUp, contentDesc = "Vol +", onClick = onVolumeUp)
            ControlIconButton(icon = Icons.Default.PowerSettingsNew, contentDesc = "Power", onClick = onPower)

            VerticalDivider(modifier = Modifier.height(24.dp).padding(horizontal = 4.dp))

            ControlIconButton(
                icon = Icons.Default.ExitToApp,
                contentDesc = "Exit",
                tint = MaterialTheme.colorScheme.error,
                onClick = onCloseContainer
            )
        }
    }
}

@Composable
private fun ControlIconButton(
    icon: ImageVector,
    contentDesc: String,
    tint: Color = MaterialTheme.colorScheme.onSurface,
    onClick: () -> Unit
) {
    IconButton(onClick = onClick) {
        Icon(
            imageVector = icon,
            contentDescription = contentDesc,
            tint = tint,
            modifier = Modifier.size(22.dp)
        )
    }
}

private fun sendKey(keyCode: Int) {
    ContainerNativeBridge.nativeSendKeyEvent(keyCode, isDown = true)
    ContainerNativeBridge.nativeSendKeyEvent(keyCode, isDown = false)
}
