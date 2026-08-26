package dev.itzkaguya.aospcontainer.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.provider.Settings
import android.util.Log
import dev.itzkaguya.aospcontainer.MainActivity
import dev.itzkaguya.aospcontainer.R
import dev.itzkaguya.aospcontainer.core.ContainerCore
import dev.itzkaguya.aospcontainer.core.ContainerNativeBridge
import dev.itzkaguya.aospcontainer.core.ContainerProcessRunner
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

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

    /** Scope for ProcessBuilder reader coroutine and other service work. */
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** Guest entrypoint for this session; defaults to full AOSP boot. */
    private var requestedInitPath: String = DEFAULT_INIT_PATH

    /** Explicit sandbox directory; null uses the default instance dir. */
    private var overrideRootfsPath: String? = null

    /**
     * Live container state from the native guest monitor thread,
     * observable by the UI without polling the service.
     */
    private val mutableContainerState =
        MutableStateFlow(ContainerNativeBridge.STATE_STOPPED)
    val containerState: StateFlow<Int> get() = mutableContainerState.asStateFlow()

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

        ContainerNativeBridge.nativeSetLifecycleListener(
            object : ContainerNativeBridge.LifecycleListener {
                override fun onStateChanged(state: Int, exitCode: Int) {
                    Log.i(TAG, "guest state=$state exitCode=$exitCode")
                    mutableContainerState.value = state
                }
            },
        )
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startAsForeground()

        when (intent?.action) {
            ACTION_STOP -> {
                stopGuest()
                stopSelf()
                return START_NOT_STICKY
            }
            else -> {
                intent?.getStringExtra(EXTRA_INIT_PATH)?.let {
                    requestedInitPath = it
                }
                intent?.getStringExtra(EXTRA_ROOTFS_PATH)?.let {
                    overrideRootfsPath = it
                }
                startGuest()
            }
        }

        handler.post(heartbeat)
        return START_STICKY
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        stopGuest()
        serviceScope.cancel()
        ContainerNativeBridge.nativeSetLifecycleListener(null)
        super.onDestroy()
    }

    /** Local binder exposing the live service to the viewport UI. */
    inner class ContainerBinder : Binder() {
        fun getService(): ContainerService = this@ContainerService
    }

    override fun onBind(intent: Intent?): IBinder = ContainerBinder()

    /* ---------------------------------------------------------------- */

    private fun startGuest() {
        if (guestPid > 0 && ContainerCore.nativeGetState() == ContainerCore.STATE_RUNNING) {
            Log.i(TAG, "guest already running pid=$guestPid")
            return
        }

        val rootfs = overrideRootfsPath
            ?: File(filesDir, "rootfs/default").absolutePath
        val socketPath = File(rootfs, ".host.sock").absolutePath

        ContainerCore.nativeIpcStart(socketPath)

        /* Default guest resolution; configurable per-instance in Phase 5. */
        val frameFd = ContainerCore.nativeCreateFrameChannel(
            DEFAULT_WIDTH, DEFAULT_HEIGHT, FRAME_SLOTS,
        )

        val pid = ContainerCore.nativeStartContainer(
            rootfsDir = rootfs,
            nativeLibDir = applicationInfo.nativeLibraryDir,
            initPath = requestedInitPath,
            extraMounts = "",
            excludePaths = "",
            fakeUid = 0,
            fakeGid = 0,
            frameFd = frameFd,
            enableSeccomp = false,
        )
        if (pid > 0) {
            guestPid = pid
            Log.i(TAG, "container started pid=$pid rootfs=$rootfs frameFd=$frameFd")
            /* Start the Kotlin-side ProcessBuilder runner in parallel so the
             * console HUD receives real guest stdout/stderr output. */
            ContainerProcessRunner.startGuest(rootfs, serviceScope)
        } else {
            Log.e(TAG, "container failed to start: errno=$pid")
            ContainerCore.nativeCloseFrameChannel()
            ContainerCore.nativeIpcStop()
        }
    }

    private fun stopGuest() {
        ContainerProcessRunner.stopGuest()
        if (guestPid > 0) {
            ContainerCore.nativeStopContainer(guestPid, GRACE_MS)
            guestPid = 0
        }
        ContainerCore.nativePresenterDetach()
        ContainerCore.nativeCloseFrameChannel()
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
        private const val DEFAULT_INIT_PATH = "/init"

        /** Diagnostic/test shells can boot via e.g. /system/bin/sh. */
        const val EXTRA_INIT_PATH = "dev.itzkaguya.aospcontainer.extra.INIT_PATH"
        const val EXTRA_ROOTFS_PATH = "dev.itzkaguya.aospcontainer.extra.ROOTFS_PATH"

        fun start(
            context: Context,
            rootfsPath: String? = null,
            initPath: String = DEFAULT_INIT_PATH,
        ) {
            context.startForegroundService(
                Intent(context, ContainerService::class.java)
                    .setAction(ACTION_START)
                    .putExtra(EXTRA_ROOTFS_PATH, rootfsPath)
                    .putExtra(EXTRA_INIT_PATH, initPath),
            )
        }

        private const val DEFAULT_WIDTH = 720
        private const val DEFAULT_HEIGHT = 1280
        private const val FRAME_SLOTS = 4

        const val ACTION_START = "dev.itzkaguya.aospcontainer.action.START"
        const val ACTION_STOP = "dev.itzkaguya.aospcontainer.action.STOP"

        fun stop(context: Context) {
            context.startForegroundService(
                Intent(context, ContainerService::class.java).setAction(ACTION_STOP),
            )
        }
    }
}
