package dev.itzkaguya.aospcontainer

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import dev.itzkaguya.aospcontainer.core.ContainerCore
import dev.itzkaguya.aospcontainer.service.ContainerService

class MainActivity : ComponentActivity() {

    private var engineVersion by mutableStateOf("")

    private val notificationPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted -> /* FGS works without it; only the notification hides */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        engineVersion = ContainerCore.nativeVersion()

        setContent {
            MaterialTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    HomeScreen(
                        engineVersion = engineVersion,
                        onStart = ::onStartClicked,
                        onStop = { ContainerService.stop(this) },
                        modifier = Modifier.padding(innerPadding),
                    )
                }
            }
        }
        requestNotificationPermissionIfNeeded()
    }

    private fun onStartClicked() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED) {
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
        ContainerService.start(this)
    }

    private fun requestNotificationPermissionIfNeeded() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED) {
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }
}

@Composable
private fun HomeScreen(
    engineVersion: String,
    onStart: () -> Unit,
    onStop: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterVertically),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(text = "AOSP Container", style = MaterialTheme.typography.headlineMedium)
        Text(
            text = stringResourceSafe(engineVersion),
            style = MaterialTheme.typography.bodyMedium,
        )
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterHorizontally)) {
            Button(onClick = onStart) {
                Text(text = "Start container")
            }
            OutlinedButton(onClick = onStop) {
                Text(text = "Stop container")
            }
        }
    }
}

/* Avoids a resource lookup while keeping the preview simple. */
@Composable
private fun stringResourceSafe(version: String): String = "Engine $version"

@Preview(showBackground = true)
@Composable
private fun HomeScreenPreview() {
    MaterialTheme {
        HomeScreen(engineVersion = "0.1.0", onStart = {}, onStop = {})
    }
}
