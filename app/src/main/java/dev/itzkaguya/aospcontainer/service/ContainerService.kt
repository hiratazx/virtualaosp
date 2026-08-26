package dev.itzkaguya.aospcontainer.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.provider.Settings
import android.util.Log
import dev.itzkaguya.aospcontainer.MainActivity
import dev.itzkaguya.aospcontainer.R
import dev.itzkaguya.aospcontainer.core.ContainerCore
import java.io.File

/**
 * Long-lived host-side coordinator for the guest container runtime.
 *
 * Runs as a specialUse foreground service so the host process is exempt
 * from Android 13+'s Phantom Process Killer while guests are alive, and
 * additionally attempts the WRITE_SECURE_SETTINGS-based PPK opt-out when
 * the user has granted it over adb.
 */
class ContainerService : Service() {

    private val handler = Handler(Looper.getMainLooper())
    private var guestPid = 0

    private val heartbeat = object : Runnable {
        override fun run() {
            refreshNotification()
            handler.postDelayed(this, HEARTBEAT_MS)
        }
    }

    override fun onCreate() {
        super.onCreate()
        createChannel()
        mitigatePhantomProcessKiller()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startAsForeground()

        when (intent?.action) {
            ACTION_STOP -> {
                stopGuest()
                stopSelf()
                return START_NOT_STICKY
            }
            else -> startGuest()
        }

        handler.post(heartbeat)
        return START_STICKY
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        stopGuest()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    /* ---------------------------------------------------------------- */

    private fun startGuest() {
        if (guestPid > 0 && ContainerCore.nativeGetState() == ContainerCore.STATE_RUNNING) {
            Log.i(TAG, "guest already running pid=$guestPid")
            return
        }

        val rootfs = File(filesDir, "rootfs/default").absolutePath
        val socketPath = File(rootfs, ".host.sock").absolutePath

        ContainerCore.nativeIpcStart(socketPath)

        val pid = ContainerCore.nativeStartContainer(
            rootfsDir = rootfs,
            nativeLibDir = applicationInfo.nativeLibraryDir,
            initPath = "/init",
            extraMounts = "",
            excludePaths = "",
            fakeUid = 0,
            fakeGid = 0,
            enableSeccomp = false,
        )
        if (pid > 0) {
            guestPid = pid
            Log.i(TAG, "container started pid=$pid rootfs=$rootfs")
        } else {
            Log.e(TAG, "container failed to start: errno=$pid")
            ContainerCore.nativeIpcStop()
        }
    }

    private fun stopGuest() {
        if (guestPid > 0) {
            ContainerCore.nativeStopContainer(guestPid, GRACE_MS)
            guestPid = 0
        }
        ContainerCore.nativeIpcStop()
    }

    /**
     * Best-effort PPK opt-out. `settings_enable_monitor_phantom_procs`
     * only honors writes from holders of WRITE_SECURE_SETTINGS (adb),
     * so this silently no-ops on stock devices without it. Running our
     * workloads behind this foreground service remains the primary
     * protection.
     */
    private fun mitigatePhantomProcessKiller() {
        runCatching {
            val current = Settings.Global.getInt(
                contentResolver,
                KEY_MONITOR_PHANTOM_PROCS,
                1,
            )
            if (current != 0) {
                Settings.Global.putInt(
                    contentResolver,
                    KEY_MONITOR_PHANTOM_PROCS,
                    0,
                )
                Log.i(TAG, "phantom process monitor disabled")
            }
        }.onFailure {
            Log.d(TAG, "PPK opt-out unavailable (${it.javaClass.simpleName}); FGS protection active")
        }
    }

    /* ---------------------------------------------------------------- */
    /* notification                                                      */
    /* ---------------------------------------------------------------- */

    private fun createChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            getString(R.string.notification_channel_name),
            NotificationManager.IMPORTANCE_LOW,
        )
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val stateName = when (ContainerCore.nativeGetState()) {
            ContainerCore.STATE_RUNNING -> getString(R.string.state_running)
            ContainerCore.STATE_STARTING -> getString(R.string.state_starting)
            ContainerCore.STATE_STOPPING -> getString(R.string.state_stopping)
            else -> getString(R.string.state_idle)
        }
        val openIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_download_done)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(getString(R.string.notification_state_fmt, stateName))
            .setOngoing(true)
            .setContentIntent(openIntent)
            .build()
    }

    private fun startAsForeground() {
        startForeground(
            NOTIFICATION_ID,
            buildNotification(),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE,
        )
    }

    private fun refreshNotification() {
        getSystemService(NotificationManager::class.java).notify(
            NOTIFICATION_ID,
            buildNotification(),
        )
    }

    companion object {
        private const val TAG = "ac.service"
        private const val CHANNEL_ID = "container_runtime"
        private const val NOTIFICATION_ID = 0xAC01
        private const val HEARTBEAT_MS = 30_000L
        private const val GRACE_MS = 2_000
        private const val KEY_MONITOR_PHANTOM_PROCS =
            "settings_enable_monitor_phantom_procs"

        const val ACTION_START = "dev.itzkaguya.aospcontainer.action.START"
        const val ACTION_STOP = "dev.itzkaguya.aospcontainer.action.STOP"

        fun start(context: Context) {
            context.startForegroundService(
                Intent(context, ContainerService::class.java).setAction(ACTION_START),
            )
        }

        fun stop(context: Context) {
            context.startForegroundService(
                Intent(context, ContainerService::class.java).setAction(ACTION_STOP),
            )
        }
    }
}
