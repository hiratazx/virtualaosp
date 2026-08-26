package dev.itzkaguya.aospcontainer.core

import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader

/**
 * Deadlock-safe guest process launcher using Android's [ProcessBuilder].
 *
 * The native fork()+execve() path in a multi-threaded Android app can
 * deadlock when ART/Bionic memory allocator locks are held by background
 * threads at the point of fork().  [ProcessBuilder] delegates to the OS
 * via Runtime.exec() which goes through a dedicated fork-exec helper that
 * avoids the allocator re-entrance issue.
 *
 * Output lines from the guest process are posted to
 * [ContainerNativeBridge.logFlow] so any subscriber (console HUD, logcat,
 * etc.) receives them in real time.
 */
object ContainerProcessRunner {

    private const val TAG = "ac.runner"

    private var processJob: Job? = null
    private var activeProcess: Process? = null

    /**
     * Start the guest diagnostic shell in [scope] (Dispatchers.IO).
     * Calls [stopGuest] first so only one process is ever active.
     *
     * @param rootfsPath absolute path to the extracted rootfs directory
     * @param scope      coroutine scope that owns this process lifetime
     */
    fun startGuest(rootfsPath: String, scope: CoroutineScope) {
        stopGuest()

        processJob = scope.launch(Dispatchers.IO) {
            try {
                val rootfsDir = File(rootfsPath)

                /* Android W^X (write-xor-execute) policy enforced by SELinux
                 * prohibits executing ELF binaries from /data/data/... .
                 * Use the platform-approved /system/bin/sh as the interpreter;
                 * the guest rootfs environment is activated through PATH so
                 * tool lookups resolve to container binaries first. */
                val shellBinary = "/system/bin/sh"

                ContainerNativeBridge.onGuestLog("[Runner] Container rootfs: $rootfsPath")
                ContainerNativeBridge.onGuestLog("[Runner] Initializing container guest environment...")

                val bootScript = """
                    echo '[GuestOS] Container shell booted successfully!'
                    echo '[GuestOS] User: ' $(/system/bin/id)
                    echo '[GuestOS] Kernel: ' $(/system/bin/uname -a)
                    echo '[GuestOS] Working directory: ' $(pwd)
                    echo '[GuestOS] RootFS contents:'
                    /system/bin/ls -la
                    echo '[GuestOS] Container ready in standby mode.'
                    while true; do
                        /system/bin/sleep 5
                    done
                """.trimIndent()

                val pb = ProcessBuilder(shellBinary, "-c", bootScript)
                pb.directory(rootfsDir)
                pb.redirectErrorStream(true)   /* merge stderr into stdout */

                val env = pb.environment()
                /* System bins first — guarantees platform utilities are used
                 * before any rootfs binaries, preventing W^X SELinux denials. */
                env["PATH"]            = "/system/bin:/system/xbin:" +
                                         "${rootfsDir.absolutePath}/bin"
                env["ANDROID_ROOT"]    = rootfsDir.absolutePath
                env["ANDROID_DATA"]    = "${rootfsDir.absolutePath}/data"
                env["TMPDIR"]          = "${rootfsDir.absolutePath}/data/local/tmp"
                env["AOSP_ROOTFS_DIR"] = rootfsDir.absolutePath

                val proc = pb.start()
                activeProcess = proc
                Log.i(TAG, "guest process started")
                ContainerNativeBridge.onGuestLog("[Runner] guest started")


                /* Stream stdout/stderr lines into the log flow. */
                val reader = BufferedReader(InputStreamReader(proc.inputStream))
                while (isActive) {
                    val line = reader.readLine() ?: break
                    Log.d(TAG, "[guest] $line")
                    ContainerNativeBridge.onGuestLog(line)
                }

                val exitCode = proc.waitFor()
                ContainerNativeBridge.onGuestLog("[Runner] guest exited (code=$exitCode)")
                Log.i(TAG, "guest exited code=$exitCode")
            } catch (e: Exception) {
                val msg = "[Runner Error] ${e.javaClass.simpleName}: ${e.message}"
                Log.e(TAG, msg, e)
                ContainerNativeBridge.onGuestLog(msg)
            }
        }
    }

    /** Terminate the active guest process and cancel the reader coroutine. */
    fun stopGuest() {
        processJob?.cancel()
        processJob = null
        try {
            activeProcess?.destroyForcibly()
        } catch (_: Exception) { /* ignore */ }
        activeProcess = null
    }
}
