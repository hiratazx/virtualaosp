package dev.itzkaguya.aospcontainer.ui.viewport

import android.view.KeyEvent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Circle
import androidx.compose.material.icons.filled.CropSquare
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.VolumeDown
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.surfaceColorAtElevation
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import dev.itzkaguya.aospcontainer.core.ContainerNativeBridge

@Composable
internal fun SidebarContent(
    onCollapse: () -> Unit,
    onBack: () -> Unit,
    onHome: () -> Unit,
    onRecents: () -> Unit,
    onVolumeUp: () -> Unit,
    onVolumeDown: () -> Unit,
    onPowerOptions: () -> Unit,
) {
    Surface(
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceColorAtElevation(8.dp).copy(alpha = 0.94f),
        tonalElevation = 8.dp,
        shadowElevation = 8.dp,
        modifier = Modifier.padding(vertical = 16.dp)
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            SidebarIconButton(
                icon = Icons.Default.ChevronRight,
                contentDesc = "Collapse",
                onClick = onCollapse
            )

            HorizontalDivider(modifier = Modifier.width(28.dp).padding(vertical = 4.dp))

            SidebarIconButton(
                icon = Icons.AutoMirrored.Filled.ArrowBack,
                contentDesc = "Back",
                onClick = onBack
            )
            SidebarIconButton(icon = Icons.Default.Circle, contentDesc = "Home", onClick = onHome)
            SidebarIconButton(icon = Icons.Default.CropSquare, contentDesc = "Recents", onClick = onRecents)

            HorizontalDivider(modifier = Modifier.width(28.dp).padding(vertical = 4.dp))

            SidebarIconButton(icon = Icons.Default.VolumeUp, contentDesc = "Volume Up", onClick = onVolumeUp)
            SidebarIconButton(icon = Icons.Default.VolumeDown, contentDesc = "Volume Down", onClick = onVolumeDown)

            HorizontalDivider(modifier = Modifier.width(28.dp).padding(vertical = 4.dp))

            SidebarIconButton(
                icon = Icons.Default.PowerSettingsNew,
                contentDesc = "Power Options",
                tint = MaterialTheme.colorScheme.error,
                onClick = onPowerOptions
            )
        }
    }
}

@Composable
private fun SidebarIconButton(
    icon: ImageVector,
    contentDesc: String,
    tint: Color = MaterialTheme.colorScheme.onSurface,
    onClick: () -> Unit
) {
    IconButton(
        onClick = onClick,
        modifier = Modifier.size(36.dp)
    ) {
        Icon(
            imageVector = icon,
            contentDescription = contentDesc,
            tint = tint,
            modifier = Modifier.size(20.dp)
        )
    }
}

internal fun sendKey(keyCode: Int) {
    ContainerNativeBridge.nativeSendKeyEvent(keyCode, isDown = true)
    ContainerNativeBridge.nativeSendKeyEvent(keyCode, isDown = false)
}
