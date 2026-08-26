package dev.itzkaguya.aospcontainer.core

/**
 * Kotlin mirror of the native `ContainerState` enum
 * (see guest_launcher.h / native_bridge.cpp).
 */
enum class NativeContainerState(val code: Int) {
    STOPPED(0),
    STARTING(1),
    RUNNING(2),
    CRASHED(3),
    TERMINATED(4);

    companion object {
        fun fromCode(code: Int): NativeContainerState =
            entries.firstOrNull { it.code == code } ?: STOPPED
    }
}
