package dev.itzkaguya.aospcontainer.ui.viewport

import android.view.MotionEvent
import android.view.View
import dev.itzkaguya.aospcontainer.core.ContainerNativeBridge

class ContainerTouchHandler : View.OnTouchListener {

    override fun onTouch(v: View, event: MotionEvent): Boolean {
        val viewWidth = v.width.toFloat()
        val viewHeight = v.height.toFloat()

        if (viewWidth <= 0f || viewHeight <= 0f) return false

        val pointerCount = event.pointerCount
        val pointerIds = IntArray(pointerCount)
        val xCoords = FloatArray(pointerCount)
        val yCoords = FloatArray(pointerCount)
        val pressures = FloatArray(pointerCount)

        for (i in 0 until pointerCount) {
            pointerIds[i] = event.getPointerId(i)
            // Passes exact pixel coordinates of current orientation directly to input driver
            xCoords[i] = event.getX(i)
            yCoords[i] = event.getY(i)
            pressures[i] = event.getPressure(i)
        }

        ContainerNativeBridge.nativeSendTouchEvent(
            action = event.actionMasked,
            pointerCount = pointerCount,
            pointerIds = pointerIds,
            xCoords = xCoords,
            yCoords = yCoords,
            pressures = pressures
        )

        return true
    }
}
