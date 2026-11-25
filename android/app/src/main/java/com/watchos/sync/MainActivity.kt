package com.watchos.sync

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.provider.Settings
import android.app.AlertDialog
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import java.util.TimeZone
import java.util.concurrent.Executors

/**
 * Простое приложение синхронизации: вводите адрес и код с экрана Sync на часах, выбираете фото и отправляете.
 * Время часов выставляется при каждом подключении.
 */
class MainActivity : Activity() {
    private lateinit var hostEdit: EditText
    private lateinit var portEdit: EditText
    private lateinit var codeEdit: EditText
    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private lateinit var pickBtn: Button
    private lateinit var timeBtn: Button
    private val worker = Executors.newSingleThreadExecutor()
    private val REQ_PICK = 1

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = getSharedPreferences("watch", Context.MODE_PRIVATE)

        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad * 2, pad, pad) }

        fun text(t: String, size: Float, color: Int = Color.DKGRAY) = TextView(this).apply { this.text = t; textSize = size; setTextColor(color) }

        root.addView(text("Watch Sync", 26f, Color.BLACK))
        root.addView(text("On the watch open Sync. It shows an address and a 6-digit code.", 14f))

        hostEdit = EditText(this).apply {
            hint = "Watch address, e.g. 192.168.0.5"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(prefs.getString("host", ""))
        }
        portEdit = EditText(this).apply {
            hint = "Port"
            inputType = InputType.TYPE_CLASS_NUMBER
            setText(prefs.getString("port", "8765"))
        }
        codeEdit = EditText(this).apply {
            hint = "Code from the watch"
            inputType = InputType.TYPE_CLASS_NUMBER
        }
        root.addView(hostEdit)
        root.addView(portEdit)
        root.addView(codeEdit)

        pickBtn = Button(this).apply { text = "Choose photos and send"; setOnClickListener { pick() } }
        timeBtn = Button(this).apply { text = "Sync time only"; setOnClickListener { run(emptyList()) } }
        root.addView(pickBtn)
        root.addView(timeBtn)

        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100; visibility = View.INVISIBLE }
        root.addView(progress)
        status = text("", 15f).apply { gravity = Gravity.START }
        root.addView(status)

        root.addView(text("Phone notifications", 20f, Color.BLACK).apply { setPadding(0, pad, 0, 0) })
        notifyStatus = text("", 14f)
        root.addView(notifyStatus)
        root.addView(Button(this).apply { text = "Allow notification access"; setOnClickListener { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) } })
        root.addView(Button(this).apply { text = "Choose apps to forward"; setOnClickListener { chooseApps() } })
        root.addView(text("On the watch: Settings > Notifications, then scan the QR code with the phone camera or Google Lens.", 13f))

        setContentView(ScrollView(this).apply { addView(root) })
        handleLink(intent)
        showNotifyStatus()
    }

    private lateinit var notifyStatus: TextView

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleLink(intent)
        showNotifyStatus()
    }

    override fun onResume() {
        super.onResume()
        try { startForegroundService(Intent(this, AiRelayService::class.java)) } catch (_: Exception) {}   // мост ИИ для часов
        showNotifyStatus()
    }

    /** Ссылка из QR: обычный режим подставляет адрес и код в поля, mode=notify сохраняет сопряжение для уведомлений. */
    private fun handleLink(i: Intent?) {
        val uri = i?.data ?: return
        if (uri.scheme != "watchsync") return
        val host = uri.getQueryParameter("host") ?: return
        val port = uri.getQueryParameter("port")?.toIntOrNull() ?: 8765
        val code = uri.getQueryParameter("code") ?: return
        if (code.length != 6 || !code.all { it in '0'..'9' }) { say("Bad link from the watch."); return }
        if (uri.getQueryParameter("mode") == "notify") {
            getSharedPreferences("watch", Context.MODE_PRIVATE).edit()
                .putString("notify_host", host).putInt("notify_port", port).putString("notify_code", code).apply()
            say("Paired for notifications with $host:$port.")
        } else {
            hostEdit.setText(host); portEdit.setText(port.toString()); codeEdit.setText(code)
            say("Address and code filled in from the watch. Press a button to send.")
        }
    }

    private fun showNotifyStatus() {
        val p = getSharedPreferences("watch", Context.MODE_PRIVATE)
        val listener = Settings.Secure.getString(contentResolver, "enabled_notification_listeners")?.contains(packageName) == true
        val pairedTxt = if (NotifyPrefs.paired(this)) "paired with ${p.getString("notify_host", "")}" else "not paired"
        val apps = p.getStringSet("notify_apps", emptySet())?.size ?: 0
        notifyStatus.text = "Watch: $pairedTxt. Access: ${if (listener) "allowed" else "not allowed"}. Apps: ${if (apps == 0) "all" else "$apps selected"}."
    }

    /** Список приложений с галочками; пустой выбор = пересылать все. */
    private fun chooseApps() {
        val pm = packageManager
        val launch = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        val apps = pm.queryIntentActivities(launch, 0).map { it.activityInfo.packageName to it.loadLabel(pm).toString() }
            .distinctBy { it.first }.sortedBy { it.second.lowercase() }
        val prefs = getSharedPreferences("watch", Context.MODE_PRIVATE)
        val chosen = (prefs.getStringSet("notify_apps", emptySet()) ?: emptySet()).toMutableSet()
        AlertDialog.Builder(this)
            .setTitle("Forward only these apps (none = all)")
            .setMultiChoiceItems(apps.map { it.second }.toTypedArray(), BooleanArray(apps.size) { apps[it].first in chosen }) { _, which, on ->
                if (on) chosen.add(apps[which].first) else chosen.remove(apps[which].first)
            }
            .setPositiveButton("OK") { _, _ -> prefs.edit().putStringSet("notify_apps", chosen).apply(); showNotifyStatus() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    override fun onDestroy() {
        worker.shutdownNow()
        super.onDestroy()
    }

    private fun pick() {
        val i = Intent(Intent.ACTION_GET_CONTENT).apply {
            type = "image/*"
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
            addCategory(Intent.CATEGORY_OPENABLE)
        }
        startActivityForResult(Intent.createChooser(i, "Choose photos"), REQ_PICK)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_PICK || resultCode != RESULT_OK || data == null) return
        val uris = ArrayList<Uri>()
        data.clipData?.let { c -> for (k in 0 until c.itemCount) uris.add(c.getItemAt(k).uri) }
        if (uris.isEmpty()) data.data?.let { uris.add(it) }
        if (uris.isNotEmpty()) run(uris)
    }

    private fun say(t: String) = runOnUiThread { status.text = t }

    private fun busy(b: Boolean) = runOnUiThread {
        pickBtn.isEnabled = !b
        timeBtn.isEnabled = !b
        progress.visibility = if (b) View.VISIBLE else View.INVISIBLE
        if (!b) progress.progress = 0
    }

    private fun run(uris: List<Uri>) {
        var host = hostEdit.text.toString().trim()
        var port = portEdit.text.toString().trim().toIntOrNull() ?: 8765
        if (host.contains(":")) {                     // можно ввести сразу "192.168.0.5:8765"
            port = host.substringAfter(":").toIntOrNull() ?: port
            host = host.substringBefore(":")
        }
        val code = codeEdit.text.toString().trim()
        if (host.isEmpty() || code.length != 6) { say("Enter the watch address and the 6-digit code."); return }
        getSharedPreferences("watch", Context.MODE_PRIVATE).edit().putString("host", host).putString("port", port.toString()).apply()

        busy(true)
        worker.execute {
            var step = "connect"                       // на каком шаге оборвалось: видно в тексте ошибки
            try {
                WatchClient(host, port).use { w ->
                    say("Connecting to $host:$port ...")
                    w.connect()
                    step = "hello"
                    val info = w.hello(code, android.os.Build.MODEL ?: "phone")
                    say("Paired. The watch has ${info.photoCount} of ${info.maxPhotos} photos.")
                    val now = System.currentTimeMillis()
                    step = "time"
                    w.setTime(now / 1000, TimeZone.getDefault().getOffset(now) / 60000)

                    if (uris.isEmpty()) { say("Time synced."); return@use }
                    step = "list"
                    val have = w.listPhotos()
                    var sent = 0
                    var skipped = 0
                    uris.forEachIndexed { index, uri ->
                        say("Photo ${index + 1} of ${uris.size} ...")
                        val id = PhotoPrep.idOf(this, uri)
                        if (id in have) { skipped++; return@forEachIndexed }
                        val p = PhotoPrep.prepare(this, uri, id)
                        step = "photo ${index + 1}"
                        w.sendPhoto(p.id, p.takenSeconds, p.thumb, p.full) { done, total ->
                            runOnUiThread { progress.progress = ((index + done.toFloat() / total) * 100 / uris.size).toInt() }
                        }
                        sent++
                    }
                    say("Done: $sent sent, $skipped already on the watch.")
                }
            } catch (e: Exception) {
                val what = e.message ?: e.javaClass.simpleName
                val hint = if (e is java.io.EOFException) " (the watch closed the connection: reopen Sync on the watch and enter the new code)" else ""
                say("Error at $step: $what$hint")
            } finally {
                busy(false)
            }
        }
    }
}
