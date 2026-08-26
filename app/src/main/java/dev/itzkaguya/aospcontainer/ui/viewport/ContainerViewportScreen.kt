package dev.itzkaguya.aospcontainer.ui.viewport

import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import dev.itzkaguya.aospcontainer.core.ContainerNativeBridge

/**
 * Live guest display: SurfaceView bound to the native presenter, touch
 * events dispatched as normalized packets through the native bridge, and
 * a control HUD injecting navigation gestures into the guest.
 */
@Composable
fun ContainerViewportScreen(
    aspectRatio: Float = 720f / 1280f,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier.fillMaxSize()) {
        AndroidView(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .aspectRatio(aspectRatio),
            factory = { context ->
                SurfaceView(context).apply {
                    holder.addCallback(ViewportSurfaceCallback())
                    setOnTouchListener { view, event ->
                        dispatchTouchEvent(event, view.width, view.height)
                        if (event.actionMasked == MotionEvent.ACTION_UP) {
                            performClick()
                        }
                        true
                    }
                }
            },
        )

        ControlHud()
    }
}

private class ViewportSurfaceCallback : SurfaceHolder.Callback {
    override fun surfaceCreated(holder: SurfaceHolder) {
        ContainerNativeBridge.nativeAttachSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        /* Presenter scales frames automatically; geometry tracked natively. */
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        ContainerNativeBridge.nativeDetachSurface()
    }
}

/* ------------------------------------------------------------------ */
/* touch dispatch                                                      */
/* ------------------------------------------------------------------ */

private const val FIXED_ONE = 65535

/** AC_TOUCH_ACTION_* values from ac_ipc_protocol.h. */
private const val ACTION_DOWN = 0
private const val ACTION_MOVE = 1
private const val ACTION_UP = 2
private const val ACTION_CANCEL = 3
private const val ACTION_POINTER_DOWN = 4
private const val ACTION_POINTER_UP = 5

private fun normalize(value: Float, size: Int): Int =
    ((value.coerceIn(0f, size.toFloat()) / size.coerceAtLeast(1)) * FIXED_ONE).toInt()

private fun actionFor(masked: Int): Int = when (masked) {
    MotionEvent.ACTION_DOWN -> ACTION_DOWN
    MotionEvent.ACTION_MOVE -> ACTION_MOVE
    MotionEvent.ACTION_UP -> ACTION_UP
    MotionEvent.ACTION_POINTER_DOWN -> ACTION_POINTER_DOWN
    MotionEvent.ACTION_POINTER_UP -> ACTION_POINTER_UP
    else -> ACTION_CANCEL
}

private fun dispatchTouchEvent(event: MotionEvent, viewWidth: Int, viewHeight: Int) {
    val action = actionFor(event.actionMasked)
    val idx = when (action) {
        ACTION_POINTER_DOWN, ACTION_POINTER_UP -> event.actionIndex
        else -> 0
    }
    for (i in 0 until event.pointerCount) {
        val pointerId = event.getPointerId(if (i == idx || event.pointerCount == 1) i else i)
        ContainerNativeBridge.nativeSendTouch(
            action = action,
            pointerId = pointerId,
            xFixed = normalize(event.getX(i), viewWidth),
            yFixed = normalize(event.getY(i), viewHeight),
            pressure = (event.pressure.coerceIn(0f, 1f) * FIXED_ONE).toInt(),
        )
    }
}

/* ------------------------------------------------------------------ */
/* control HUD                                                         */
/* ------------------------------------------------------------------ */

/**
 * Navigation strip: taps land on the guest's gesture-nav zones along the
 * bottom edge of the virtual display (25% / 50% / 75% width).
 */
@Composable
private fun ControlHud() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp, Alignment.CenterHorizontally),
    ) {
        HudButton("Back", 0.25f)
        HudButton("Home", 0.50f)
        HudButton("Recents", 0.75f)
    }
}

@Composable
private fun HudButton(label: String, xFraction: Float) {
    OutlinedButton(onClick = { sendSyntheticTap(xFraction) }) {
        Text(label, style = MaterialTheme.typography.labelMedium)
    }
}

private fun sendSyntheticTap(xFraction: Float) {
    val x = (xFraction * FIXED_ONE).toInt()
    val y = (0.97f * FIXED_ONE).toInt()
    ContainerNativeBridge.nativeSendTouch(ACTION_DOWN, 0, x, y, FIXED_ONE)
    ContainerNativeBridge.nativeSendTouch(ACTION_UP, 0, x, y, FIXED_ONE)
}
