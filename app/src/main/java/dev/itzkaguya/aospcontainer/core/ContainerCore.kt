package dev.itzkaguya.aospcontainer.core

/**
 * JNI entry point into `libcontainer_core.so`, the host-side coordinator.
 */
object ContainerCore {

    /** Container lifecycle states mirrored from the native launcher. */
    const val STATE_IDLE: Int = 0
    const val STATE_STARTING: Int = 1
    const val STATE_RUNNING: Int = 2
    const val STATE_STOPPING: Int = 3
    const val STATE_EXITED: Int = 4

    init {
        System.loadLibrary("container_core")
    }

    /** Engine version reported by the native core. */
    external fun nativeVersion(): String

    /** Liveness probe for the native library. */
    external fun nativePing(): Boolean

    /**
     * Fork+exec one guest process with libfake.so injected via LD_PRELOAD.
     * Returns the guest pid (>0), or a negative -errno on failure.
     */
    external fun nativeStartContainer(
        rootfsDir: String,
        nativeLibDir: String,
        initPath: String,
        extraMounts: String,
        excludePaths: String,
        fakeUid: Int,
        fakeGid: Int,
        frameFd: Int,
        enableSeccomp: Boolean,
    ): Int

    /** SIGTERM the guest process group; escalate to SIGKILL after [graceMs]. */
    external fun nativeStopContainer(pid: Int, graceMs: Int): Boolean

    /** One of the STATE_* constants. */
    external fun nativeGetState(): Int

    /** Start the host-side IPC endpoint at [socketPath] (inside sandbox). */
    external fun nativeIpcStart(socketPath: String): Boolean

    /** Shut the IPC endpoint down and remove its socket node. */
    external fun nativeIpcStop()

    /** Send a packet of [type] to every connected guest client.
     * Returns the number of clients it was delivered to. */
    external fun nativeIpcBroadcast(type: Int, payload: ByteArray?): Int

    /** Create the shared frame region; returns its fd for passing into
     * [nativeStartContainer], or a negative -errno. */
    external fun nativeCreateFrameChannel(width: Int, height: Int, slots: Int): Int

    /** Tear down presenter + channel singleton. */
    external fun nativeCloseFrameChannel()

    /** Start presenting frames from the channel onto [surface]. */
    external fun nativePresenterAttachSurface(surface: android.view.Surface): Boolean

    /** Stop the presentation loop and release the surface reference. */
    external fun nativePresenterDetach()

    /** Read a guest ELF's PT_INTERP ("" when absent/invalid). */
    external fun nativeReadInterp(elfPath: String): String

    /** Overwrite PT_INTERP in place; 0 on success, negative -errno
     * (-ENOSPC when the replacement exceeds the existing segment). */
    external fun nativePatchInterp(elfPath: String, newInterp: String): Int
}
