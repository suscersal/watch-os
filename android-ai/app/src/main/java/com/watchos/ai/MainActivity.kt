package com.watchos.ai

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.Inet4Address
import java.net.NetworkInterface
import java.net.URL

/** Настройки моста ИИ: ключ OpenRouter, модель и кнопки запуска/остановки сервиса. */
class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var key: EditText
    private lateinit var model: EditText
    private lateinit var system: EditText

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)

        val prefs = getSharedPreferences("ai", MODE_PRIVATE)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad * 2, pad, pad) }

        root.addView(TextView(this).apply { text = "Watch AI"; textSize = 24f })
        root.addView(TextView(this).apply {
            text = "Часы шлют вопрос на этот телефон по сети раздачи, а он спрашивает OpenRouter. " +
                "Включи VPN для этого приложения и раздачу Wi-Fi, подключи часы к раздаче."
            setPadding(0, pad / 2, 0, pad)
        })

        key = EditText(this).apply {
            hint = "OpenRouter API key (sk-or-...)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            setText(prefs.getString("key", ""))
        }
        model = EditText(this).apply {
            hint = "Model"
            setText(prefs.getString("model", AiService.DEFAULT_MODEL))
        }
        system = EditText(this).apply {
            hint = "System prompt (answer format)"
            setText(prefs.getString("system", "")?.ifBlank { null } ?: AiService.DEFAULT_SYSTEM)
            minLines = 4
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
        }
        root.addView(key)
        root.addView(model)
        root.addView(system)
        root.addView(Button(this).apply {
            text = "Reset prompt to default"
            setOnClickListener { system.setText(AiService.DEFAULT_SYSTEM) }
        })
        root.addView(Button(this).apply {
            text = "Choose model (free / paid list)"
            setOnClickListener { loadModels() }
        })

        root.addView(Button(this).apply {
            text = "Save and start"
            setOnClickListener {
                prefs.edit().putString("key", key.text.toString().trim())
                    .putString("model", model.text.toString().trim().ifEmpty { AiService.DEFAULT_MODEL })
                    .putString("system", system.text.toString().trim()).apply()
                startForegroundService(Intent(this@MainActivity, AiService::class.java))
                status.postDelayed({ refresh() }, 600)
            }
        })
        root.addView(Button(this).apply {
            text = "Stop"
            setOnClickListener {
                stopService(Intent(this@MainActivity, AiService::class.java))
                status.postDelayed({ refresh() }, 300)
            }
        })

        // Без этого Android может «замораживать» приложение в фоне (например, пока открыт эмулятор часов),
        // и ответ приходит только когда Watch AI выводят на экран.
        root.addView(Button(this).apply {
            text = "Allow background work (battery)"
            setOnClickListener {
                try {
                    startActivity(Intent(android.provider.Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                        android.net.Uri.parse("package:$packageName")))
                } catch (_: Exception) {
                    startActivity(Intent(android.provider.Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
                }
            }
        })

        status = TextView(this).apply { setPadding(0, pad, 0, 0) }
        root.addView(status)
        setContentView(ScrollView(this).apply { addView(root) })
    }

    override fun onResume() { super.onResume(); refresh() }

    private fun refresh() {
        val ips = try {
            NetworkInterface.getNetworkInterfaces().toList().filter { it.isUp && !it.isLoopback }
                .flatMap { n -> n.inetAddresses.toList().filterIsInstance<Inet4Address>().map { "${n.name}: ${it.hostAddress}" } }
        } catch (_: Exception) { emptyList() }
        status.text = (if (AiService.running) "Running, port ${AiService.PORT}" else "Stopped") +
            "\n" + AiService.info + "\n\nAddresses:\n" + ips.joinToString("\n")
    }

    // ---------- выбор модели ----------

    private class ModelItem(val id: String, val name: String, val free: Boolean, val perMillion: Double)

    /** Список моделей OpenRouter (публичный, ключ не нужен): GET /api/v1/models. Нужен VPN для этого приложения. */
    private fun loadModels() {
        Toast.makeText(this, "Loading models...", Toast.LENGTH_SHORT).show()
        Thread {
            try {
                val c = URL("https://openrouter.ai/api/v1/models").openConnection() as HttpURLConnection
                c.connectTimeout = 10000
                c.readTimeout = 20000
                val txt = try { c.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() } } finally { c.disconnect() }
                val arr = JSONObject(txt).getJSONArray("data")
                val list = ArrayList<ModelItem>()
                for (i in 0 until arr.length()) {
                    val m = arr.getJSONObject(i)
                    val outs = m.optJSONObject("architecture")?.optJSONArray("output_modalities")
                    if (outs != null && (0 until outs.length()).none { outs.optString(it) == "text" }) continue  // не чат-модель (картинки и т.п.)
                    val pr = m.optJSONObject("pricing") ?: continue
                    val p = pr.optString("prompt", "").toDoubleOrNull() ?: continue
                    val q = pr.optString("completion", "").toDoubleOrNull() ?: continue
                    list.add(ModelItem(m.getString("id"), m.optString("name", ""), p == 0.0 && q == 0.0, p * 1_000_000))
                }
                list.sortWith(compareBy<ModelItem>({ !it.free }, { it.id }))
                // Роутер бесплатных моделей: сам выбирает любую свободную :free модель, поэтому закреплён сверху
                list.removeAll { it.id == "openrouter/free" }
                list.add(0, ModelItem("openrouter/free", "Free Models Router", true, 0.0))
                runOnUiThread { showModelDialog(list) }
            } catch (e: Exception) {
                runOnUiThread { Toast.makeText(this, "Models: ${e.message} (is VPN on for this app?)", Toast.LENGTH_LONG).show() }
            }
        }.start()
    }

    private fun label(m: ModelItem): String {
        val price = when {
            m.free -> "free"
            m.perMillion < 0 -> "price varies (router)"
            else -> "$" + String.format(java.util.Locale.US, "%.2f", m.perMillion) + " / 1M input tokens"
        }
        return m.id + "\n" + price
    }

    private fun showModelDialog(all: List<ModelItem>) {
        val pad = (12 * resources.displayMetrics.density).toInt()
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, 0) }
        val freeOnly = CheckBox(this).apply { text = "Free only"; isChecked = true }
        val limitsNote = TextView(this).apply {
            text = "openrouter/free = сам берёт любую свободную бесплатную модель. Лимит бесплатных: 20 запросов/мин и 50/день на весь аккаунт (1000/день после пополнения от \$10)."
            textSize = 12f
        }
        val search = EditText(this).apply { hint = "Search" }
        val listView = ListView(this)
        box.addView(freeOnly)
        box.addView(limitsNote)
        box.addView(search)
        box.addView(listView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, (resources.displayMetrics.heightPixels * 0.5).toInt()))

        var shown: List<ModelItem> = emptyList()
        val adapter = ArrayAdapter(this, android.R.layout.simple_list_item_1, ArrayList<String>())
        listView.adapter = adapter
        fun refreshList() {
            val q = search.text.toString().trim().lowercase()
            shown = all.filter { (!freeOnly.isChecked || it.free) && (q.isEmpty() || it.id.lowercase().contains(q) || it.name.lowercase().contains(q)) }
            adapter.clear()
            adapter.addAll(shown.map { label(it) })
            adapter.notifyDataSetChanged()
        }
        freeOnly.setOnCheckedChangeListener { _, _ -> refreshList() }
        search.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, st: Int, c: Int, a: Int) {}
            override fun onTextChanged(s: CharSequence?, st: Int, b: Int, c: Int) {}
            override fun afterTextChanged(s: Editable?) { refreshList() }
        })

        val dlg = AlertDialog.Builder(this).setTitle("Models (${all.size})").setView(box).setNegativeButton("Cancel", null).create()
        listView.setOnItemClickListener { _, _, pos, _ ->
            val id = shown[pos].id
            model.setText(id)
            getSharedPreferences("ai", MODE_PRIVATE).edit().putString("model", id).apply()
            Toast.makeText(this, "Model: $id", Toast.LENGTH_SHORT).show()
            dlg.dismiss()
        }
        refreshList()
        dlg.show()
    }
}
