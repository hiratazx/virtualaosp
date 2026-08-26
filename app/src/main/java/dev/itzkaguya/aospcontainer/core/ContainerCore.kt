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
}
