package com.watchos.sync

import android.app.Notification
import android.content.Context
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import java.util.concurrent.Executors

/** Настройки сопряжения для уведомлений (SharedPreferences "watch"): host, port, notify_code, notify_apps. */
object NotifyPrefs {
    fun paired(c: Context): Boolean {
        val p = c.getSharedPreferences("watch", Context.MODE_PRIVATE)
        return !p.getString("notify_host", "").isNullOrEmpty() && (p.getString("notify_code", "") ?: "").length == 6
    }
}

/**
 * Пересылает уведомления телефона на часы. На каждое уведомление: подключиться, войти постоянным кодом, отправить MESSAGE, закрыться.
 * Если часы недоступны, уведомление пропускается (две попытки с паузой). Нужен доступ к уведомлениям в системных настройках.
 */
class NotifyService : NotificationListenerService() {
    private val worker = Executors.newSingleThreadExecutor()

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        val prefs = getSharedPreferences("watch", Context.MODE_PRIVATE)
        if (!NotifyPrefs.paired(this) || !prefs.getBoolean("notify_forward", true)) return
        val n = sbn.notification ?: return
        if (sbn.packageName == packageName) return
        if (sbn.isOngoing || (n.flags and Notification.FLAG_GROUP_SUMMARY) != 0) return       // музыкальные плееры, сводки групп
        val allowed = prefs.getStringSet("notify_apps", emptySet()) ?: emptySet()
        if (allowed.isNotEmpty() && sbn.packageName !in allowed) return                         // пустой список = все приложения

        val ex = n.extras
        val title = ex.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val text = (ex.getCharSequence(Notification.EXTRA_BIG_TEXT) ?: ex.getCharSequence(Notification.EXTRA_TEXT))?.toString() ?: ""
        if (title.isEmpty() && text.isEmpty()) return
        val app = try {
            packageManager.getApplicationLabel(packageManager.getApplicationInfo(sbn.packageName, 0)).toString()
        } catch (e: Exception) { sbn.packageName }

        val host = prefs.getString("notify_host", "") ?: return
        val port = prefs.getInt("notify_port", 8765)
        val code = prefs.getString("notify_code", "") ?: return
        val id = (sbn.packageName.hashCode() * 31 + sbn.id) * 31 + (sbn.tag?.hashCode() ?: 0)   // то же уведомление = тот же id
        val whenSec = (if (n.`when` > 0) n.`when` else System.currentTimeMillis()) / 1000

        worker.execute {
            for (attempt in 0 until 2) {
                try {
                    WatchClient(host, port).use { w ->
                        w.connect(3000)
                        w.hello(code, "phone")
                        w.sendMessage(id, whenSec, app, title, text)
                    }
                    return@execute
                } catch (e: Exception) {
                    try { Thread.sleep(2000) } catch (_: InterruptedException) { return@execute }
                }
            }
        }
    }

    override fun onDestroy() {
        worker.shutdownNow()
        super.onDestroy()
    }
}
