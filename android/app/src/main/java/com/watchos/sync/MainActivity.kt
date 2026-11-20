package com.watchos.sync

import android.app.Activity
import android.content.Context
import android.content.Intent
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

        setContentView(ScrollView(this).apply { addView(root) })
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
            try {
                WatchClient(host, port).use { w ->
                    say("Connecting to $host:$port ...")
                    w.connect()
                    val info = w.hello(code, android.os.Build.MODEL ?: "phone")
                    say("Paired. The watch has ${info.photoCount} of ${info.maxPhotos} photos.")
                    val now = System.currentTimeMillis()
                    w.setTime(now / 1000, TimeZone.getDefault().getOffset(now) / 60000)

                    if (uris.isEmpty()) { say("Time synced."); return@use }
                    val have = w.listPhotos()
                    var sent = 0
                    var skipped = 0
                    uris.forEachIndexed { index, uri ->
                        say("Photo ${index + 1} of ${uris.size} ...")
                        val id = PhotoPrep.idOf(this, uri)
                        if (id in have) { skipped++; return@forEachIndexed }
                        val p = PhotoPrep.prepare(this, uri, id)
                        w.sendPhoto(p.id, p.takenSeconds, p.thumb, p.full) { done, total ->
                            runOnUiThread { progress.progress = ((index + done.toFloat() / total) * 100 / uris.size).toInt() }
                        }
                        sent++
                    }
                    say("Done: $sent sent, $skipped already on the watch.")
                }
            } catch (e: Exception) {
                say("Error: ${e.message ?: e.javaClass.simpleName}")
            } finally {
                busy(false)
            }
        }
    }
}
