package dev.itzkaguya.aospcontainer.ui

import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import dev.itzkaguya.aospcontainer.core.ContainerCore
import dev.itzkaguya.aospcontainer.input.ContainerInput

/**
 * Live guest display embedded in Compose via a SurfaceView.
 *
 * The holder's surface is handed to the native presenter which blits the
 * shared-memory frame channel onto it at vsync cadence; touch events are
 * forwarded through the input bridge as normalized packets.
 */
@Composable
fun ContainerViewport(
    modifier: Modifier = Modifier,
    aspectRatio: Float = 720f / 1280f,
) {
    AndroidView(
        modifier = modifier
            .fillMaxSize()
            .aspectRatio(aspectRatio),
        factory = { context ->
            SurfaceView(context).apply {
                holder.setKeepScreenOn(true)
                holder.addCallback(ViewportHolderCallback())
                setOnTouchListener { _, event ->
                    val consumed = ContainerInput.dispatch(event)
                    if (event.actionMasked == MotionEvent.ACTION_UP) {
                        performClick()
                    }
                    consumed
                }
            }
        },
        update = { view ->
            ContainerInput.setViewport(view.width.coerceAtLeast(1), view.height.coerceAtLeast(1))
        },
    )
}

private class ViewportHolderCallback : SurfaceHolder.Callback {
    override fun surfaceCreated(holder: SurfaceHolder) {
        ContainerCore.nativePresenterAttachSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        ContainerInput.setViewport(width.coerceAtLeast(1), height.coerceAtLeast(1))
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        ContainerCore.nativePresenterDetach()
    }
}
