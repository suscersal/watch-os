package com.watchos.sync

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import java.net.ServerSocket
import java.util.concurrent.Executors

/**
 * Принимает запросы часов к ИИ на порту 8787 и передаёт их в Watch AI (127.0.0.1:8788, см. [Relay]).
 * Это приложение работает без VPN, поэтому ответы уходят в раздачу Wi-Fi, а не в туннель VPN.
 */
class AiRelayService : Service() {
    private var server: ServerSocket? = null
    private var wake: PowerManager.WakeLock? = null
    private val pool = Executors.newCachedThreadPool()

    companion object {
        private const val CHANNEL = "ai_relay"
        @Volatile var running = false
        @Volatile var info = ""
    }

    override fun onBind(i: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        getSystemService(NotificationManager::class.java)
            .createNotificationChannel(NotificationChannel(CHANNEL, tr("AI relay", "Ретранслятор ИИ"), NotificationManager.IMPORTANCE_LOW))
        val n = Notification.Builder(this, CHANNEL)
            .setContentTitle(tr("Watch AI relay is running", "Ретранслятор Watch AI работает"))
            .setContentText(tr("Passing watch requests to Watch AI (port ${Relay.PORT})", "Запросы часов передаются в Watch AI (порт ${Relay.PORT})"))
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) startForeground(2, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        else startForeground(2, n)
        if (wake == null) {
            wake = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "watchsync:airelay")
                .also { it.acquire() }
        }
        if (server == null) startServer()
        return START_STICKY
    }

    override fun onDestroy() {
        running = false
        try { server?.close() } catch (_: Exception) {}
        server = null
        wake?.let { if (it.isHeld) it.release() }
        wake = null
        pool.shutdownNow()
        super.onDestroy()
    }

    private fun startServer() {
        Thread {
            try {
                val ss = ServerSocket(Relay.PORT, 8)
                server = ss
                running = true
                info = "Listening on port ${Relay.PORT}"
                while (!ss.isClosed) {
                    val c = ss.accept()
                    pool.execute { Relay.serve(c) { pool.execute(it) } }
                }
            } catch (e: Exception) {
                if (server != null) info = "Relay stopped: ${e.message}"
            } finally {
                running = false
                server = null
            }
        }.start()
    }
}
