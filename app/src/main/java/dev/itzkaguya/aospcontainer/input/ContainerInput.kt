package dev.itzkaguya.aospcontainer.input

import android.view.MotionEvent
import dev.itzkaguya.aospcontainer.core.ContainerCore
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Converts host [MotionEvent]s into normalized container input packets
 * and broadcasts them to the guest over the IPC daemon.
 *
 * Coordinates are normalized against the viewport size at dispatch time,
 * so the guest maps them onto its own display geometry regardless of the
 * host window size or orientation.
 */
object ContainerInput {

    /** Wire type from ac_ipc_protocol.h. */
    private const val TYPE_INPUT_TOUCH = 0x10

    private const val ACTION_DOWN = 0
    private const val ACTION_MOVE = 1
    private const val ACTION_UP = 2
    private const val ACTION_CANCEL = 3
    private const val ACTION_POINTER_DOWN = 4
    private const val ACTION_POINTER_UP = 5

    /** Latest known viewport dimensions used for normalization. */
    @Volatile
    var viewportWidth: Int = 0
        private set

    @Volatile
    var viewportHeight: Int = 0
        private set

    fun setViewport(width: Int, height: Int) {
        viewportWidth = width.coerceAtLeast(1)
        viewportHeight = height.coerceAtLeast(1)
    }

    /** Entry point for SurfaceView touch callbacks. */
    fun dispatch(event: MotionEvent): Boolean {
        if (viewportWidth == 0 || viewportHeight == 0) return false

        val handled: Boolean
        when (event.actionMasked) {
            MotionEvent.ACTION_POINTER_DOWN -> {
                val idx = event.actionIndex
                send(ACTION_POINTER_DOWN, event.getPointerId(idx), event.getX(idx), event.getY(idx))
                handled = true
            }
            MotionEvent.ACTION_POINTER_UP -> {
                val idx = event.actionIndex
                send(ACTION_POINTER_UP, event.getPointerId(idx), event.getX(idx), event.getY(idx))
                handled = true
            }
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE,
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                val action = when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> ACTION_DOWN
                    MotionEvent.ACTION_MOVE -> ACTION_MOVE
                    MotionEvent.ACTION_UP -> ACTION_UP
                    else -> ACTION_CANCEL
                }
                for (i in 0 until event.pointerCount) {
                    send(action, event.getPointerId(i), event.getX(i), event.getY(i))
                }
                handled = true
            }
            else -> handled = false
        }
        return handled
    }

    private fun send(action: Int, pointerId: Int, x: Float, y: Float) {
        val packet = ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(action)
            putInt(pointerId)
            putInt(normalize(x, viewportWidth))
            putInt(normalize(y, viewportHeight))
            putInt(65535)
        }.array()
        ContainerCore.nativeIpcBroadcast(TYPE_INPUT_TOUCH, packet)
    }

    /** Float coordinate in [0,size] -> 16.16 fixed point in [0,65535]. */
    private fun normalize(value: Float, size: Int): Int =
        ((value.coerceIn(0f, size.toFloat()) / size) * 65535f).toInt()
}
