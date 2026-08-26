package dev.itzkaguya.aospcontainer.core

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow

/**
 * Kotlin interface to the container native bridge (`libcontainer_core`).
 *
 * Symbol names must stay in sync with native_bridge.cpp.
 */
object ContainerNativeBridge {

    /** Mirrors the native ContainerState enum. */
    const val STATE_STOPPED: Int = 0
    const val STATE_STARTING: Int = 1
    const val STATE_RUNNING: Int = 2
    const val STATE_CRASHED: Int = 3
    const val STATE_TERMINATED: Int = 4

    /** Async lifecycle notifications delivered from the guest monitor thread. */
    interface LifecycleListener {
        fun onStateChanged(state: Int, exitCode: Int)
    }

    init {
        System.loadLibrary("container_core")
    }

    /* ---- Guest log stream (Kotlin-only, no JNI) ---- */

    private val _logFlow = MutableSharedFlow<String>(replay = 50, extraBufferCapacity = 200)

    /** Live stream of lines from the guest process stdout/stderr. */
    val logFlow: SharedFlow<String> = _logFlow.asSharedFlow()

    /**
     * Post a single log line from [ContainerProcessRunner] or any other
     * guest output source into [logFlow]. Safe to call from any thread.
     */
    fun onGuestLog(line: String) {
        _logFlow.tryEmit(line)
    }

    /** Fork/exec the guest init binary with libfake.so preloaded. */
    external fun nativeStartContainer(
        rootfsPath: String,
        libfakePath: String,
        initBinaryPath: String,
    ): Boolean

    /** Signal the guest process group (default SIGTERM). */
    external fun nativeStopContainer(signal: Int, timeoutMs: Int): Boolean

    /** One of the STATE_* constants. */
    external fun nativeGetContainerState(): Int

    /**
     * Registers the single lifecycle listener (replaces any previous
     * one); pass null to unregister.
     */
    external fun nativeSetLifecycleListener(listener: LifecycleListener?)

    /** Start presenting guest frames onto [surface]. */
    external fun nativeAttachSurface(surface: android.view.Surface): Boolean

    /** Stop the presentation loop and release the surface reference. */
    external fun nativeDetachSurface()

    /**
     * Dispatch one normalized touch event to all connected guests.
     * Coordinates are 16.16 fixed point; returns delivery count.
     */
    external fun nativeSendTouch(
        action: Int,
        pointerId: Int,
        xFixed: Int,
        yFixed: Int,
        pressure: Int,
    ): Int

    /* ---- dynamic surface lifecycle (orientation-aware) ---- */

    /** Bind a freshly created surface to the EGL renderer. */
    external fun nativeOnSurfaceCreated(surface: android.view.Surface)

    /** Re-bind + resize on rotation, fold, or multi-window changes. */
    external fun nativeOnSurfaceChanged(surface: android.view.Surface, width: Int, height: Int)

    /** Release the window; EGL context is preserved for rebind. */
    external fun nativeOnSurfaceDestroyed()

    /**
     * Multi-pointer batch in HOST PIXELS of the current orientation;
     * the native layer normalizes against the live surface size.
     */
    external fun nativeSendTouchEvent(
        action: Int,
        pointerCount: Int,
        pointerIds: IntArray,
        xCoords: FloatArray,
        yCoords: FloatArray,
        pressures: FloatArray,
    ): Int

    /** Android keycode injection; [isDown] mirrors KeyEvent down/up. */
    external fun nativeSendKeyEvent(keyCode: Int, isDown: Boolean): Int
}
