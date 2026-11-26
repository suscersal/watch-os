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

private fun toHex(b: ByteArray) = b.joinToString("") { "%02x".format(it) }

private fun fromHex(s: String?): ByteArray? {
    if (s == null || s.length != Proto.TOKEN_LEN * 2) return null
    return try { ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() } } catch (_: NumberFormatException) { null }
}

/**
 * Простое приложение синхронизации: один раз вводите адрес и код с экрана Sync на часах, дальше телефон подключается сам
 * (часы выдают постоянный токен, он хранится в SharedPreferences "watch"). Выбираете фото и отправляете.
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
    private lateinit var audioBtn: Button
    private val worker = Executors.newSingleThreadExecutor()
    private val REQ_PICK = 1
    private val REQ_AUDIO = 2

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = getSharedPreferences("watch", Context.MODE_PRIVATE)

        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad * 2, pad, pad) }

        fun text(t: String, size: Float, color: Int = Color.DKGRAY) = TextView(this).apply { this.text = t; textSize = size; setTextColor(color) }

        root.addView(text("Watch Sync", 26f, Color.BLACK))
        root.addView(text("First time: on the watch open Sync and enter the address and 6-digit code here. After that the phone connects by itself, no code needed.", 14f))

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
            hint = "Code from the watch (empty once paired)"
            inputType = InputType.TYPE_CLASS_NUMBER
        }
        root.addView(hostEdit)
        root.addView(portEdit)
        root.addView(codeEdit)

        pickBtn = Button(this).apply { text = "Choose photos and send"; setOnClickListener { pick() } }
        timeBtn = Button(this).apply { text = "Sync time only"; setOnClickListener { run(emptyList()) } }
        audioBtn = Button(this).apply { text = "Choose music and send"; setOnClickListener { pickAudio() } }
        root.addView(pickBtn)
        root.addView(audioBtn)
        root.addView(timeBtn)
        root.addView(Button(this).apply { text = "Forget this watch"; setOnClickListener { forgetWatch() } })

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
        showPairState()
    }

    private lateinit var notifyStatus: TextView

    private fun savedToken(): ByteArray? = fromHex(getSharedPreferences("watch", Context.MODE_PRIVATE).getString("token", null))

    private fun showPairState() {
        val host = getSharedPreferences("watch", Context.MODE_PRIVATE).getString("host", "") ?: ""
        if (savedToken() != null && host.isNotEmpty()) status.text = "Paired with $host. No code needed: just press a button."
    }

    /** Токен удаляется на телефоне; на часах его можно забыть в Sync (кнопка Forget), тогда он перестаёт работать совсем. */
    private fun forgetWatch() {
        getSharedPreferences("watch", Context.MODE_PRIVATE).edit().remove("token").apply()
        say("Forgotten on this phone. To pair again open Sync on the watch and enter the code. To also remove it on the watch press Forget there.")
    }

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

    private fun pickAudio() {
        val i = Intent(Intent.ACTION_GET_CONTENT).apply {
            type = "audio/*"
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
            addCategory(Intent.CATEGORY_OPENABLE)
        }
        startActivityForResult(Intent.createChooser(i, "Choose music"), REQ_AUDIO)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if ((requestCode != REQ_PICK && requestCode != REQ_AUDIO) || resultCode != RESULT_OK || data == null) return
        val uris = ArrayList<Uri>()
        data.clipData?.let { c -> for (k in 0 until c.itemCount) uris.add(c.getItemAt(k).uri) }
        if (uris.isEmpty()) data.data?.let { uris.add(it) }
        if (uris.isNotEmpty()) run(uris, requestCode == REQ_AUDIO)
    }

    private fun say(t: String) = runOnUiThread { status.text = t }

    private fun busy(b: Boolean) = runOnUiThread {
        pickBtn.isEnabled = !b
        timeBtn.isEnabled = !b
        audioBtn.isEnabled = !b
        progress.visibility = if (b) View.VISIBLE else View.INVISIBLE
        if (!b) progress.progress = 0
    }

    private fun run(uris: List<Uri>, audio: Boolean = false) {
        var host = hostEdit.text.toString().trim()
        var port = portEdit.text.toString().trim().toIntOrNull() ?: 8765
        if (host.contains(":")) {                     // можно ввести сразу "192.168.0.5:8765"
            port = host.substringAfter(":").toIntOrNull() ?: port
            host = host.substringBefore(":")
        }
        val code = codeEdit.text.toString().trim()
        val saved = if (code.isEmpty()) savedToken() else null          // введён код - входим по коду и получаем новый токен
        if (host.isEmpty() || (saved == null && code.length != 6)) { say("Enter the watch address and the 6-digit code (first time only)."); return }
        getSharedPreferences("watch", Context.MODE_PRIVATE).edit().putString("host", host).putString("port", port.toString()).apply()

        busy(true)
        worker.execute {
            var step = "connect"                       // на каком шаге оборвалось: видно в тексте ошибки
            val tracks = ArrayList<AudioPrep.Result>()
            try {
                if (audio) {                           // раскодируем до подключения: долгий трек не должен держать молчащее соединение
                    step = "convert"
                    uris.forEachIndexed { index, uri ->
                        say("Converting track ${index + 1} of ${uris.size} ...")
                        tracks.add(AudioPrep.prepare(this, uri) { p ->
                            runOnUiThread { progress.progress = ((index + p / 100f) * 100 / uris.size).toInt() }
                        })
                    }
                    runOnUiThread { progress.progress = 0 }
                    step = "connect"
                }
                WatchClient(host, port).use { w ->
                    say("Connecting to $host:$port ...")
                    w.connect()
                    step = "hello"
                    val model = android.os.Build.MODEL ?: "phone"
                    val info = if (saved != null) w.helloToken(saved, model) else w.hello(code, model)
                    if (saved == null) {                    // вошли по коду: просим постоянный токен, чтобы больше код не вводить
                        step = "pair"
                        try {
                            val t = w.requestToken()
                            getSharedPreferences("watch", Context.MODE_PRIVATE).edit().putString("token", toHex(t)).apply()
                            runOnUiThread { codeEdit.setText("") }   // код одноразовый, больше не нужен
                        } catch (_: WatchException) { /* старая прошивка или постоянный код уведомлений: работаем с кодом, как раньше */ }
                        step = "hello"
                    }
                    say("Paired. The watch has ${info.photoCount} of ${info.maxPhotos} photos.")
                    val now = System.currentTimeMillis()
                    step = "time"
                    w.setTime(now / 1000, TimeZone.getDefault().getOffset(now) / 60000)

                    if (uris.isEmpty()) { say("Time synced."); return@use }
                    if (audio) {
                        var sentA = 0
                        var skippedA = 0
                        tracks.forEachIndexed { index, t ->
                            say("Track ${index + 1} of ${tracks.size}: ${t.name}")
                            step = "track ${index + 1}"
                            val ok = w.sendAudio(t.name, t.file.length(), t.crc, t.cover, { t.file.inputStream() }) { done, total ->
                                runOnUiThread { progress.progress = ((index + done.toFloat() / total) * 100 / tracks.size).toInt() }
                            }
                            if (ok) sentA++ else skippedA++
                        }
                        say("Done: $sentA sent, $skippedA already on the watch.")
                        return@use
                    }
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
                var hint = if (e is java.io.EOFException) " (the watch closed the connection: reopen Sync on the watch and enter the new code)" else ""
                if (saved != null && e is WatchException && e.message == Proto.errorText(2)) {
                    getSharedPreferences("watch", Context.MODE_PRIVATE).edit().remove("token").apply()     // часы этот телефон забыли
                    hint = " (the watch forgot this phone: open Sync on the watch and enter the new code)"
                } else if (saved != null && step == "connect") {
                    hint = " (is the watch awake and on Wi-Fi? If its address changed, enter the new one from Sync)"
                }
                say("Error at $step: $what$hint")
            } finally {
                tracks.forEach { it.file.delete() }
                busy(false)
            }
        }
    }
}
