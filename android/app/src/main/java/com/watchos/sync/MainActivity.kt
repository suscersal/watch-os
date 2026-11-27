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
import android.view.WindowManager
import android.widget.Button
import android.widget.CheckBox
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
    private val REQ_FW = 3
    private val REQ_FULL = 4
    private lateinit var usbBtn: Button
    private lateinit var keepBox: CheckBox
    private lateinit var fwBtn: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = getSharedPreferences("watch", Context.MODE_PRIVATE)

        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad * 2, pad, pad) }

        fun text(t: String, size: Float, color: Int = Color.DKGRAY) = TextView(this).apply { this.text = t; textSize = size; setTextColor(color) }

        root.addView(text("Watch Sync", 26f, Color.BLACK))
        root.addView(text(tr("First time: on the watch open Sync and enter the address and 6-digit code here. After that the phone connects by itself, no code needed.", "Первый раз: откройте на часах Sync и введите здесь адрес и 6-значный код. Дальше телефон подключается сам, код больше не нужен."), 14f))

        hostEdit = EditText(this).apply {
            hint = tr("Watch address, e.g. 192.168.0.5", "Адрес часов, например 192.168.0.5")
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(prefs.getString("host", ""))
        }
        portEdit = EditText(this).apply {
            hint = tr("Port", "Порт")
            inputType = InputType.TYPE_CLASS_NUMBER
            setText(prefs.getString("port", "8765"))
        }
        codeEdit = EditText(this).apply {
            hint = tr("Code from the watch (empty once paired)", "Код с часов (после сопряжения не нужен)")
            inputType = InputType.TYPE_CLASS_NUMBER
        }
        root.addView(hostEdit)
        root.addView(portEdit)
        root.addView(codeEdit)

        pickBtn = Button(this).apply { text = tr("Choose photos and send", "Выбрать фото и отправить"); setOnClickListener { pick() } }
        timeBtn = Button(this).apply { text = tr("Sync time only", "Только синхронизировать время"); setOnClickListener { run(emptyList()) } }
        audioBtn = Button(this).apply { text = tr("Choose music and send", "Выбрать музыку и отправить"); setOnClickListener { pickAudio() } }
        root.addView(pickBtn)
        root.addView(audioBtn)
        root.addView(timeBtn)
        fwBtn = Button(this).apply { text = tr("Update firmware (watch_os.bin)", "Обновить прошивку (watch_os.bin)"); setOnClickListener { pickFirmware() } }
        root.addView(fwBtn)
        root.addView(text(tr("Full image over USB (rescue, new partition table, bootloader). Plug the watch into the phone with a USB-C OTG cable.", "Полный образ по USB (восстановление, новая таблица разделов, загрузчик). Подключите часы к телефону кабелем USB-C OTG."), 13f))
        keepBox = CheckBox(this).apply { text = tr("Keep my data (PIN, apps, photos, music, Wi-Fi)", "Сохранить мои данные (PIN, приложения, фото, музыка, Wi-Fi)"); isChecked = true }
        usbBtn = Button(this).apply { text = tr("Flash full image over USB (watch-os-full.bin)", "Прошить полный образ по USB (watch-os-full.bin)"); setOnClickListener { pickFull() } }
        root.addView(keepBox)
        root.addView(usbBtn)
        root.addView(Button(this).apply { text = tr("Forget this watch", "Забыть эти часы"); setOnClickListener { forgetWatch() } })

        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100; visibility = View.INVISIBLE }
        root.addView(progress)
        status = text("", 15f).apply { gravity = Gravity.START }
        root.addView(status)

        root.addView(text(tr("Phone notifications", "Уведомления с телефона"), 20f, Color.BLACK).apply { setPadding(0, pad, 0, 0) })
        notifyStatus = text("", 14f)
        root.addView(notifyStatus)
        root.addView(Button(this).apply { text = tr("Allow notification access", "Разрешить доступ к уведомлениям"); setOnClickListener { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) } })
        root.addView(Button(this).apply { text = tr("Choose apps to forward", "Выбрать приложения для пересылки"); setOnClickListener { chooseApps() } })
        root.addView(text(tr("On the watch: Settings > Notifications, then scan the QR code with the phone camera or Google Lens.", "На часах: Settings > Notifications, затем отсканируйте QR-код камерой телефона или Google Lens."), 13f))

        setContentView(ScrollView(this).apply { addView(root) })
        handleLink(intent)
        showNotifyStatus()
        showPairState()
    }

    private lateinit var notifyStatus: TextView

    private fun savedToken(): ByteArray? = fromHex(getSharedPreferences("watch", Context.MODE_PRIVATE).getString("token", null))

    private fun showPairState() {
        val host = getSharedPreferences("watch", Context.MODE_PRIVATE).getString("host", "") ?: ""
        if (savedToken() != null && host.isNotEmpty()) status.text = tr("Paired with $host. No code needed: just press a button.", "Сопряжено с $host. Код не нужен: просто нажмите кнопку.")
    }

    /** Токен удаляется на телефоне; на часах его можно забыть в Sync (кнопка Forget), тогда он перестаёт работать совсем. */
    private fun forgetWatch() {
        getSharedPreferences("watch", Context.MODE_PRIVATE).edit().remove("token").apply()
        say(tr("Forgotten on this phone. To pair again open Sync on the watch and enter the code. To also remove it on the watch press Forget there.", "Телефон забыл эти часы. Чтобы сопрячь снова, откройте Sync на часах и введите код. Чтобы забыть и на часах, нажмите там Forget."))
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
        if (code.length != 6 || !code.all { it in '0'..'9' }) { say(tr("Bad link from the watch.", "Неверная ссылка с часов.")); return }
        if (uri.getQueryParameter("mode") == "notify") {
            getSharedPreferences("watch", Context.MODE_PRIVATE).edit()
                .putString("notify_host", host).putInt("notify_port", port).putString("notify_code", code).apply()
            say(tr("Paired for notifications with $host:$port.", "Сопряжено для уведомлений с $host:$port."))
        } else {
            hostEdit.setText(host); portEdit.setText(port.toString()); codeEdit.setText(code)
            say(tr("Address and code filled in from the watch. Press a button to send.", "Адрес и код подставлены с часов. Нажмите кнопку для отправки."))
        }
    }

    private fun showNotifyStatus() {
        val p = getSharedPreferences("watch", Context.MODE_PRIVATE)
        val listener = Settings.Secure.getString(contentResolver, "enabled_notification_listeners")?.contains(packageName) == true
        val pairedTxt = if (NotifyPrefs.paired(this)) tr("paired with ${p.getString("notify_host", "")}", "сопряжены с ${p.getString("notify_host", "")}") else tr("not paired", "не сопряжены")
        val apps = p.getStringSet("notify_apps", emptySet())?.size ?: 0
        notifyStatus.text = tr("Watch: $pairedTxt. Access: ${if (listener) "allowed" else "not allowed"}. Apps: ${if (apps == 0) "all" else "$apps selected"}.", "Часы: $pairedTxt. Доступ: ${if (listener) "разрешён" else "не разрешён"}. Приложения: ${if (apps == 0) "все" else "выбрано $apps"}.")
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
            .setTitle(tr("Forward only these apps (none = all)", "Пересылать только эти приложения (пусто = все)"))
            .setMultiChoiceItems(apps.map { it.second }.toTypedArray(), BooleanArray(apps.size) { apps[it].first in chosen }) { _, which, on ->
                if (on) chosen.add(apps[which].first) else chosen.remove(apps[which].first)
            }
            .setPositiveButton("OK") { _, _ -> prefs.edit().putStringSet("notify_apps", chosen).apply(); showNotifyStatus() }
            .setNegativeButton(tr("Cancel", "Отмена"), null)
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
        startActivityForResult(Intent.createChooser(i, tr("Choose photos", "Выбор фото")), REQ_PICK)
    }

    private fun pickAudio() {
        val i = Intent(Intent.ACTION_GET_CONTENT).apply {
            type = "audio/*"
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
            addCategory(Intent.CATEGORY_OPENABLE)
        }
        startActivityForResult(Intent.createChooser(i, tr("Choose music", "Выбор музыки")), REQ_AUDIO)
    }

    private fun pickFirmware() {
        val i = Intent(Intent.ACTION_GET_CONTENT).apply { type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE) }
        startActivityForResult(Intent.createChooser(i, tr("Choose watch_os.bin", "Выберите watch_os.bin")), REQ_FW)
    }

    private fun pickFull() {
        val i = Intent(Intent.ACTION_GET_CONTENT).apply { type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE) }
        startActivityForResult(Intent.createChooser(i, tr("Choose watch-os-full.bin", "Выберите watch-os-full.bin")), REQ_FULL)
    }

    private fun confirmFull(uri: Uri) {
        val keep = keepBox.isChecked
        val what = if (keep) tr("The program and bootloader are replaced; PIN, apps, photos, music and Wi-Fi are kept.", "Заменяются программа и загрузчик; PIN, приложения, фото, музыка и Wi-Fi сохраняются.")
                   else tr("The watch is erased completely: PIN, apps, photos and music are lost.", "Часы стираются полностью: PIN, приложения, фото и музыка будут потеряны.")
        AlertDialog.Builder(this)
            .setTitle(tr("Flash over USB?", "Прошить по USB?"))
            .setMessage(tr("$what Do not unplug the cable until it says Done.\n\n${fileName(uri)}", "$what Не отключайте кабель, пока не появится «Готово».\n\n${fileName(uri)}"))
            .setPositiveButton(tr("Flash", "Прошить")) { _, _ -> flashFull(uri, keep) }
            .setNegativeButton(tr("Cancel", "Отмена"), null)
            .show()
    }

    /** Полный образ по USB прямо с телефона (ROM-загрузчик ESP32-S3). keep: nvs и storage не трогаем, данные остаются. */
    private fun flashFull(uri: Uri, keep: Boolean) {
        busy(true)
        runOnUiThread { window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
        worker.execute {
            var step = "file"
            var port: UsbCdcPort? = null
            try {
                val img = contentResolver.openInputStream(uri)!!.use { it.readBytes() }
                val plan = FlashPlan.plan(img, keep)
                step = "usb"
                val dev = UsbFlash.find(this)
                    ?: throw FlashException(tr("No watch on USB. Plug it in with a USB-C OTG cable; if it is still not found, hold the BOOT button while plugging.", "Часы не найдены по USB. Подключите кабелем USB-C OTG; если всё равно не находятся, зажмите кнопку BOOT при подключении."))
                say(tr("Allow USB access in the system dialog.", "Разрешите доступ к USB в системном окне."))
                if (!UsbFlash.permission(this, dev)) throw FlashException(tr("USB access was not allowed", "Доступ к USB не разрешён"))
                val p = UsbFlash.open(this, dev)
                port = p
                step = "flash"
                val ef = EspFlasher(p) { say(it) }
                ef.connect()
                ef.write(plan) { pct -> runOnUiThread { progress.progress = pct } }
                say(tr("Done. The watch restarts by itself", "Готово. Часы перезагрузятся сами") + (if (keep) tr("; your data is kept.", "; данные сохранены.") else tr(". Set it up again (PIN, apps).", ". Настройте их заново (PIN, приложения).")))
            } catch (e: Exception) {
                say(tr("Error at $step: ${e.message ?: e.javaClass.simpleName}", "Ошибка на шаге ${stepName(step)}: ${e.message ?: e.javaClass.simpleName}"))
            } finally {
                port?.close()
                runOnUiThread { window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
                busy(false)
            }
        }
    }

    private fun fileName(uri: Uri): String {
        var n: String? = null
        contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c -> if (c.moveToFirst()) n = c.getString(0) }
        return n ?: uri.lastPathSegment ?: "file"
    }

    /** Прошивку часы принимают только по одноразовому коду с экрана Sync (токен не годится), поэтому код обязателен. */
    private fun confirmFirmware(uri: Uri) {
        val code = codeEdit.text.toString().trim()
        if (hostEdit.text.toString().trim().isEmpty() || code.length != 6) {
            say(tr("Firmware update needs the watch address and the 6-digit code shown on the Sync screen of the watch (a saved pairing is not enough).", "Для обновления прошивки нужны адрес часов и 6-значный код с экрана Sync на часах (сохранённого сопряжения недостаточно)."))
            return
        }
        AlertDialog.Builder(this)
            .setTitle(tr("Update firmware?", "Обновить прошивку?"))
            .setMessage(tr("The watch installs this file and restarts. Keep it above 30% or on the charger and do not close Sync. " +
                "If the new firmware does not start, the watch goes back to the old one.\n\n${fileName(uri)}",
                "Часы установят этот файл и перезагрузятся. Заряд должен быть выше 30% или часы на зарядке; не закрывайте Sync. " +
                "Если новая прошивка не запустится, часы вернутся к старой.\n\n${fileName(uri)}"))
            .setPositiveButton(tr("Update", "Обновить")) { _, _ -> updateFirmware(uri) }
            .setNegativeButton(tr("Cancel", "Отмена"), null)
            .show()
    }

    private fun updateFirmware(uri: Uri) {
        var host = hostEdit.text.toString().trim()
        var port = portEdit.text.toString().trim().toIntOrNull() ?: 8765
        if (host.contains(":")) { port = host.substringAfter(":").toIntOrNull() ?: port; host = host.substringBefore(":") }
        val code = codeEdit.text.toString().trim()
        busy(true)
        worker.execute {
            var step = "check"
            try {
                var size = 0L
                val crc = java.util.zip.CRC32()
                val head = ByteArray(512)
                var hn = 0
                contentResolver.openInputStream(uri)!!.use { ins ->
                    val b = ByteArray(65536)
                    while (true) {
                        val n = ins.read(b)
                        if (n < 0) break
                        if (hn < head.size) { val k = minOf(n, head.size - hn); System.arraycopy(b, 0, head, hn, k); hn += k }
                        crc.update(b, 0, n)
                        size += n
                    }
                }
                firmwareProblem(head.copyOf(hn), size)?.let { throw WatchException(it) }
                step = "connect"
                WatchClient(host, port).use { w ->
                    say(tr("Connecting to $host:$port ...", "Подключаюсь к $host:$port ..."))
                    w.connect()
                    step = "hello"
                    w.hello(code, android.os.Build.MODEL ?: "phone")
                    step = "firmware"
                    say(tr("Installing firmware (${size / 1024} KB). Keep this screen open.", "Установка прошивки (${size / 1024} КБ). Не закрывайте этот экран."))
                    w.sendFirmware(size, crc.value, { contentResolver.openInputStream(uri)!! }) { done, total ->
                        runOnUiThread { progress.progress = (done * 100 / total).toInt() }
                    }
                    say(tr("Firmware installed. The watch restarts in a few seconds and is back in about 10 s.", "Прошивка установлена. Часы перезагрузятся через несколько секунд и вернутся примерно через 10 с."))
                }
            } catch (e: Exception) {
                say(tr("Error at $step: ${e.message ?: e.javaClass.simpleName}", "Ошибка на шаге ${stepName(step)}: ${e.message ?: e.javaClass.simpleName}"))
            } finally {
                busy(false)
            }
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_FULL) { if (resultCode == RESULT_OK) data?.data?.let { confirmFull(it) }; return }
        if (requestCode == REQ_FW) { if (resultCode == RESULT_OK) data?.data?.let { confirmFirmware(it) }; return }
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
        fwBtn.isEnabled = !b
        usbBtn.isEnabled = !b
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
        if (host.isEmpty() || (saved == null && code.length != 6)) { say(tr("Enter the watch address and the 6-digit code (first time only).", "Введите адрес часов и 6-значный код (только в первый раз).")); return }
        getSharedPreferences("watch", Context.MODE_PRIVATE).edit().putString("host", host).putString("port", port.toString()).apply()

        busy(true)
        worker.execute {
            var step = "connect"                       // на каком шаге оборвалось: видно в тексте ошибки
            val tracks = ArrayList<AudioPrep.Result>()
            try {
                if (audio) {                           // раскодируем до подключения: долгий трек не должен держать молчащее соединение
                    step = "convert"
                    uris.forEachIndexed { index, uri ->
                        say(tr("Converting track ${index + 1} of ${uris.size} ...", "Конвертирую трек ${index + 1} из ${uris.size} ..."))
                        tracks.add(AudioPrep.prepare(this, uri) { p ->
                            runOnUiThread { progress.progress = ((index + p / 100f) * 100 / uris.size).toInt() }
                        })
                    }
                    runOnUiThread { progress.progress = 0 }
                    step = "connect"
                }
                WatchClient(host, port).use { w ->
                    say(tr("Connecting to $host:$port ...", "Подключаюсь к $host:$port ..."))
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
                    say(tr("Paired. The watch has ${info.photoCount} of ${info.maxPhotos} photos.", "Сопряжено. На часах ${info.photoCount} из ${info.maxPhotos} фото."))
                    val now = System.currentTimeMillis()
                    step = "time"
                    w.setTime(now / 1000, TimeZone.getDefault().getOffset(now) / 60000)

                    if (uris.isEmpty()) { say(tr("Time synced.", "Время синхронизировано.")); return@use }
                    if (audio) {
                        var sentA = 0
                        var skippedA = 0
                        tracks.forEachIndexed { index, t ->
                            say(tr("Track ${index + 1} of ${tracks.size}: ${t.name}", "Трек ${index + 1} из ${tracks.size}: ${t.name}"))
                            step = "track ${index + 1}"
                            val ok = w.sendAudio(t.name, t.file.length(), t.crc, t.cover, { t.file.inputStream() }) { done, total ->
                                runOnUiThread { progress.progress = ((index + done.toFloat() / total) * 100 / tracks.size).toInt() }
                            }
                            if (ok) sentA++ else skippedA++
                        }
                        say(tr("Done: $sentA sent, $skippedA already on the watch.", "Готово: отправлено $sentA, уже на часах $skippedA."))
                        return@use
                    }
                    step = "list"
                    val have = w.listPhotos()
                    var sent = 0
                    var skipped = 0
                    uris.forEachIndexed { index, uri ->
                        say(tr("Photo ${index + 1} of ${uris.size} ...", "Фото ${index + 1} из ${uris.size} ..."))
                        val id = PhotoPrep.idOf(this, uri)
                        if (id in have) { skipped++; return@forEachIndexed }
                        val p = PhotoPrep.prepare(this, uri, id)
                        step = "photo ${index + 1}"
                        w.sendPhoto(p.id, p.takenSeconds, p.thumb, p.full) { done, total ->
                            runOnUiThread { progress.progress = ((index + done.toFloat() / total) * 100 / uris.size).toInt() }
                        }
                        sent++
                    }
                    say(tr("Done: $sent sent, $skipped already on the watch.", "Готово: отправлено $sent, уже на часах $skipped."))
                }
            } catch (e: Exception) {
                val what = e.message ?: e.javaClass.simpleName
                var hint = if (e is java.io.EOFException) tr(" (the watch closed the connection: reopen Sync on the watch and enter the new code)", " (часы закрыли соединение: заново откройте Sync на часах и введите новый код)") else ""
                if (saved != null && e is WatchException && e.message == Proto.errorText(2)) {
                    getSharedPreferences("watch", Context.MODE_PRIVATE).edit().remove("token").apply()     // часы этот телефон забыли
                    hint = tr(" (the watch forgot this phone: open Sync on the watch and enter the new code)", " (часы забыли этот телефон: откройте Sync на часах и введите новый код)")
                } else if (saved != null && step == "connect") {
                    hint = tr(" (is the watch awake and on Wi-Fi? If its address changed, enter the new one from Sync)", " (часы включены и в Wi-Fi? Если адрес изменился, введите новый из Sync)")
                }
                say(tr("Error at $step: $what$hint", "Ошибка на шаге ${stepName(step)}: $what$hint"))
            } finally {
                tracks.forEach { it.file.delete() }
                busy(false)
            }
        }
    }
}
