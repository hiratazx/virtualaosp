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

                /* Resolve shell binary: /bin/sh first (flattened GSI layout),
                 * then /system/bin/sh, then host shell as last resort. */
                val shellBinary = when {
                    File(rootfsDir, "bin/sh").exists() -> {
                        File(rootfsDir, "bin/sh").setExecutable(true, false)
                        File(rootfsDir, "bin/sh").absolutePath
                    }
                    File(rootfsDir, "system/bin/sh").exists() -> {
                        File(rootfsDir, "system/bin/sh").setExecutable(true, false)
                        File(rootfsDir, "system/bin/sh").absolutePath
                    }
                    else -> "/system/bin/sh"
                }

                /* Ensure linker is executable if present. */
                File(rootfsDir, "bin/linker64").takeIf { it.exists() }
                    ?.setExecutable(true, false)

                ContainerNativeBridge.onGuestLog("[Runner] rootfs: $rootfsPath")
                ContainerNativeBridge.onGuestLog("[Runner] shell:  $shellBinary")

                val bootScript = """
                    echo '[GuestOS] Container shell booted!'
                    echo "[GuestOS] uid=$(id -u) gid=$(id -g)"
                    echo "[GuestOS] kernel=$(uname -r)"
                    echo '[GuestOS] rootfs layout:'
                    ls -la /
                    echo '[GuestOS] Entering standby loop...'
                    while true; do sleep 5; done
                """.trimIndent()

                val pb = ProcessBuilder(shellBinary, "-c", bootScript)
                pb.directory(rootfsDir)
                pb.redirectErrorStream(true)   /* merge stderr into stdout */

                val env = pb.environment()
                env["PATH"]         = "${rootfsDir.absolutePath}/bin:" +
                                      "${rootfsDir.absolutePath}/system/bin:/system/bin"
                env["ANDROID_ROOT"] = rootfsDir.absolutePath
                env["ANDROID_DATA"] = "${rootfsDir.absolutePath}/data"
                env["TMPDIR"]       = "${rootfsDir.absolutePath}/data/local/tmp"

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
