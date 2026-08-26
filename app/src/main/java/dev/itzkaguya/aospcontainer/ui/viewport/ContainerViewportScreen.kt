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
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChevronLeft
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.surfaceColorAtElevation
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import dev.itzkaguya.aospcontainer.core.ContainerNativeBridge
import dev.itzkaguya.aospcontainer.service.ContainerService
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@SuppressLint("ClickableViewAccessibility")
@Composable
fun ContainerViewportScreen(
    onExitContainer: () -> Unit,
    modifier: Modifier = Modifier,
    rootfsPath: String? = null,
    initBinary: String = "/system/bin/sh"
) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()
    var containerService by remember { mutableStateOf<ContainerService?>(null) }
    var isBound by remember { mutableStateOf(false) }

    var isSidebarExpanded by remember { mutableStateOf(false) }
    var showPowerDialog by remember { mutableStateOf(false) }
    var showConsole by remember { mutableStateOf(true) }
    val consoleLogs = remember { mutableStateListOf<String>() }

    /* isContainerReady: cleared by three independent paths so the spinner
     * always dismisses regardless of which arrives first:
     *   1. native lifecycle callback fires STATE_RUNNING (via InvokeStateCallback)
     *   2. SurfaceHolder.surfaceCreated — native window is attached
     *   3. 1.5 s fallback timeout in case both above race or are missed */
    var isContainerReady by remember { mutableStateOf(false) }

    // Path 3: hard timeout so the overlay never blocks the user permanently
    LaunchedEffect(Unit) {
        delay(1500)
        isContainerReady = true
    }

    // Console seed log
    LaunchedEffect(Unit) {
        consoleLogs.add("[Engine] Initializing container runtime...")
        consoleLogs.add("[Engine] Guest rootfs: ${rootfsPath ?: "<default>"}")
        consoleLogs.add("[Engine] Init binary: $initBinary")
        consoleLogs.add("[Engine] Frame channel initializing (720x1280 x4 slots)...")
        consoleLogs.add("[Guest]  Process spawning via linker64...")
        consoleLogs.add("[Guest]  Waiting for compositor output...")
    }

    // Stream real guest stdout/stderr into the console HUD via logFlow
    LaunchedEffect(Unit) {
        ContainerNativeBridge.logFlow.collect { line ->
            consoleLogs.add(line)
            // Cap at 200 lines to bound memory
            if (consoleLogs.size > 200) consoleLogs.removeAt(0)
        }
    }

    // Path 1: native STATE_RUNNING dispatched via InvokeStateCallback → StateFlow
    val containerState by (containerService?.containerState
        ?: kotlinx.coroutines.flow.MutableStateFlow(0)).collectAsState()
    LaunchedEffect(containerState) {
        if (containerState == ContainerNativeBridge.STATE_RUNNING) isContainerReady = true
    }

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
        // 1. Core Viewport Display
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { ctx ->
                SurfaceView(ctx).apply {
                    setZOrderOnTop(false)
                    setOnTouchListener(ContainerTouchHandler())

                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) {
                            ContainerNativeBridge.nativeOnSurfaceCreated(holder.surface)
                            // Path 2: surface attached — native renderer is live
                            isContainerReady = true
                        }

                        override fun surfaceChanged(
                            holder: SurfaceHolder,
                            format: Int,
                            width: Int,
                            height: Int
                        ) {
                            ContainerNativeBridge.nativeOnSurfaceChanged(holder.surface, width, height)
                        }

                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                            ContainerNativeBridge.nativeOnSurfaceDestroyed()
                        }
                    })
                }
            }
        )

        // 2. Diagnostic Console HUD (top 35% overlay)
        if (showConsole) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .fillMaxHeight(0.35f)
                    .align(Alignment.TopStart)
                    .padding(16.dp),
                color = Color.Black.copy(alpha = 0.78f),
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "Container Console",
                            color = Color(0xFF00E676),  // Material Green A400
                            style = MaterialTheme.typography.labelMedium,
                            fontFamily = FontFamily.Monospace
                        )
                        IconButton(
                            onClick = { showConsole = false },
                            modifier = Modifier.size(20.dp)
                        ) {
                            Icon(
                                Icons.Default.Close,
                                contentDescription = "Close console",
                                tint = Color.LightGray,
                                modifier = Modifier.size(14.dp)
                            )
                        }
                    }
                    HorizontalDivider(
                        color = Color.DarkGray,
                        modifier = Modifier.padding(vertical = 4.dp)
                    )
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(consoleLogs) { line ->
                            Text(
                                text = line,
                                color = Color(0xFFB0BEC5),  // Blue Grey 200
                                fontFamily = FontFamily.Monospace,
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                    }
                }
            }
        }

        // 3. Outside click area to auto-collapse the sidebar
        if (isSidebarExpanded) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .clickable(
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null
                    ) {
                        isSidebarExpanded = false
                    }
            )
        }

        // 3. Collapsible Floating Trigger Pill (When Collapsed)
        AnimatedVisibility(
            visible = !isSidebarExpanded,
            enter = fadeIn() + scaleIn(),
            exit = fadeOut() + scaleOut(),
            modifier = Modifier
                .align(Alignment.CenterEnd)
                .padding(end = 4.dp)
        ) {
            Surface(
                onClick = { isSidebarExpanded = true },
                shape = RoundedCornerShape(topStart = 16.dp, bottomStart = 16.dp),
                color = MaterialTheme.colorScheme.surfaceColorAtElevation(8.dp).copy(alpha = 0.75f),
                tonalElevation = 6.dp,
                modifier = Modifier.size(width = 24.dp, height = 56.dp)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.Default.ChevronLeft,
                        contentDescription = "Expand Sidebar",
                        tint = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.size(18.dp)
                    )
                }
            }
        }

        // 5. Floating Sidebar Menu (When Expanded)
        AnimatedVisibility(
            visible = isSidebarExpanded,
            enter = slideInHorizontally(initialOffsetX = { it }) + fadeIn(),
            exit = slideOutHorizontally(targetOffsetX = { it }) + fadeOut(),
            modifier = Modifier
                .align(Alignment.CenterEnd)
                .padding(end = 12.dp)
        ) {
            SidebarContent(
                onCollapse = { isSidebarExpanded = false },
                onBack = { sendKey(KeyEvent.KEYCODE_BACK) },
                onHome = { sendKey(KeyEvent.KEYCODE_HOME) },
                onRecents = { sendKey(KeyEvent.KEYCODE_APP_SWITCH) },
                onVolumeUp = { sendKey(KeyEvent.KEYCODE_VOLUME_UP) },
                onVolumeDown = { sendKey(KeyEvent.KEYCODE_VOLUME_DOWN) },
                onPowerOptions = { showPowerDialog = true },
                isConsoleVisible = showConsole,
                onToggleConsole = { showConsole = !showConsole },
            )
        }

        // 6. Startup Loading Overlay — dismissed by surface attach or STATE_RUNNING
        if (!isContainerReady) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.72f)),
                contentAlignment = Alignment.Center
            ) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    CircularProgressIndicator(
                        color = Color(0xFF00E676),
                        strokeWidth = 3.dp,
                        modifier = Modifier.size(52.dp)
                    )
                    Text(
                        text = "Booting container\u2026",
                        color = Color.White,
                        style = MaterialTheme.typography.bodyMedium,
                        fontFamily = FontFamily.Monospace
                    )
                    if (rootfsPath != null) {
                        Text(
                            text = rootfsPath.substringAfterLast("/"),
                            color = Color(0xFF90A4AE),
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }
            }
        }

        // 7. Power Actions Modal Dialog
        if (showPowerDialog) {
            AlertDialog(
                onDismissRequest = { showPowerDialog = false },
                icon = {
                    Icon(
                        imageVector = Icons.Default.PowerSettingsNew,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.error,
                        modifier = Modifier.size(32.dp)
                    )
                },
                title = {
                    Text(
                        text = "Guest OS Power Management",
                        style = MaterialTheme.typography.titleLarge,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.fillMaxWidth()
                    )
                },
                text = {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 8.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = "Select an action to manage the container instance.",
                            style = MaterialTheme.typography.bodyMedium,
                            textAlign = TextAlign.Center,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )

                        Spacer(modifier = Modifier.height(8.dp))

                        // Restart Container Button
                        FilledTonalButton(
                            onClick = {
                                showPowerDialog = false
                                isSidebarExpanded = false
                                coroutineScope.launch {
                                    ContainerService.stop(context)
                                    delay(500)
                                    ContainerService.start(context, rootfsPath, initBinary)
                                }
                            },
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(48.dp),
                            shape = RoundedCornerShape(12.dp)
                        ) {
                            Icon(
                                Icons.Default.Refresh,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(Modifier.width(8.dp))
                            Text("Restart Container")
                        }

                        // Shutdown Container Button
                        Button(
                            onClick = {
                                showPowerDialog = false
                                isSidebarExpanded = false
                                ContainerService.stop(context)
                                onExitContainer()
                            },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = MaterialTheme.colorScheme.error,
                                contentColor = MaterialTheme.colorScheme.onError
                            ),
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(48.dp),
                            shape = RoundedCornerShape(12.dp)
                        ) {
                            Icon(
                                Icons.Default.PowerSettingsNew,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(Modifier.width(8.dp))
                            Text("Shutdown & Quit", fontWeight = FontWeight.Bold)
                        }

                        // Cancel Button
                        OutlinedButton(
                            onClick = { showPowerDialog = false },
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(48.dp),
                            shape = RoundedCornerShape(12.dp)
                        ) {
                            Text("Cancel")
                        }
                    }
                },
                confirmButton = {},
                dismissButton = {}
            )
        }
    }
}
