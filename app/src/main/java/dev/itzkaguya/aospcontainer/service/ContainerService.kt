package dev.itzkaguya.aospcontainer.service

import android.app.Service
import android.content.Intent
import android.os.IBinder

/**
 * Long-lived host-side coordinator for the guest container runtime.
 * Full foreground lifecycle + PPK mitigation lands in Phase 2.
 */
class ContainerService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null
}
