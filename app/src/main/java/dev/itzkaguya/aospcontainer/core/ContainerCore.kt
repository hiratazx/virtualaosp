package dev.itzkaguya.aospcontainer.core

/**
 * JNI entry point into `libcontainer_core.so`, the host-side coordinator.
 */
object ContainerCore {

    init {
        System.loadLibrary("container_core")
    }

    /** Engine version reported by the native core. */
    external fun nativeVersion(): String

    /** Liveness probe for the native library. */
    external fun nativePing(): Boolean
}
