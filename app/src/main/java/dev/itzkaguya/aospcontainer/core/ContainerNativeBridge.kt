package dev.itzkaguya.aospcontainer.core

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
}
