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
import android.view.View
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

/** Настройки моста ИИ: провайдер, ключ и модель (у каждого провайдера свои), кнопки запуска/остановки сервиса. */
class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var key: EditText
    private lateinit var model: EditText
    private lateinit var system: EditText
    private lateinit var providerBtn: Button
    private lateinit var cur: AiService.Companion.Provider   // выбранный сейчас провайдер

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)

        val prefs = getSharedPreferences("ai", MODE_PRIVATE)
        AiService.migrate(prefs)
        cur = AiService.provider(prefs.getString("provider", null))
        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad * 2, pad, pad) }

        root.addView(TextView(this).apply { text = "Watch AI"; textSize = 24f })
        root.addView(TextView(this).apply {
            text = "Часы шлют вопрос на этот телефон по сети раздачи, а он спрашивает выбранного провайдера ИИ. " +
                "Включи VPN для этого приложения и раздачу Wi-Fi, подключи часы к раздаче."
            setPadding(0, pad / 2, 0, pad)
        })

        providerBtn = Button(this).apply { setOnClickListener { chooseProvider() } }
        key = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        model = EditText(this).apply { hint = "Model" }
        showProvider()
        system = EditText(this).apply {
            hint = "System prompt (answer format)"
            setText(prefs.getString("system", "")?.ifBlank { null } ?: AiService.DEFAULT_SYSTEM)
            minLines = 4
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
        }
        root.addView(providerBtn)
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
                saveCurrent()
                prefs.edit().putString("system", system.text.toString().trim()).apply()
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
        status.text = (if (AiService.running) "Running, loopback port ${AiService.PORT}" else "Stopped") + "\nProvider: ${cur.name}" +
            "\n" + AiService.info + "\n\nThe watch connects through the Watch Sync app (port 8787, no VPN): keep it installed and open it once."
    }

    // ---------- выбор провайдера ----------

    private fun prefs() = getSharedPreferences("ai", MODE_PRIVATE)

    /** Подставляет в поля ключ и модель выбранного провайдера (ключ других провайдеров не трогается). */
    private fun showProvider() {
        val p = prefs()
        providerBtn.text = "Provider: ${cur.name} (tap to change)"
        key.hint = "${cur.name} API key (${cur.keyHint})"
        key.setText(p.getString("key_${cur.id}", ""))
        model.setText(p.getString("model_${cur.id}", null)?.ifBlank { null } ?: cur.defaultModel)
    }

    /** Сохраняет ключ и модель в записи текущего провайдера. */
    private fun saveCurrent() {
        prefs().edit()
            .putString("provider", cur.id)
            .putString("key_${cur.id}", key.text.toString().trim())
            .putString("model_${cur.id}", model.text.toString().trim().ifEmpty { cur.defaultModel })
            .apply()
    }

    private fun chooseProvider() {
        val p = prefs()
        val names = AiService.PROVIDERS.map { it.name + if (!p.getString("key_${it.id}", "").isNullOrEmpty()) "  (key saved)" else "" }
        AlertDialog.Builder(this).setTitle("AI provider")
            .setItems(names.toTypedArray()) { _, i ->
                saveCurrent()                                  // токен старого провайдера остаётся в его записи
                cur = AiService.PROVIDERS[i]
                p.edit().putString("provider", cur.id).apply()
                showProvider()
                refresh()
            }
            .setNegativeButton("Cancel", null).show()
    }

    // ---------- выбор модели ----------

    private class ModelItem(val id: String, val name: String, val free: Boolean, val perMillion: Double)  // perMillion = NaN: цена неизвестна

    /**
     * OpenRouter: публичный список с ценами (GET /api/v1/models, ключ не нужен).
     * Остальные провайдеры: GET {base}/models с ключом, без цен. Нужен VPN для этого приложения.
     */
    private fun loadModels() {
        val prov = cur
        val apiKey = key.text.toString().trim()
        if (prov.id != "openrouter" && apiKey.isEmpty()) {
            Toast.makeText(this, "Enter the ${prov.name} API key first", Toast.LENGTH_LONG).show()
            return
        }
        Toast.makeText(this, "Loading models...", Toast.LENGTH_SHORT).show()
        Thread {
            try {
                val c = URL(prov.base + "/models").openConnection() as HttpURLConnection
                c.connectTimeout = 10000
                c.readTimeout = 20000
                if (prov.id != "openrouter") c.setRequestProperty("Authorization", "Bearer $apiKey")
                val txt = try { c.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() } } finally { c.disconnect() }
                val arr = JSONObject(txt).getJSONArray("data")
                val list = ArrayList<ModelItem>()
                for (i in 0 until arr.length()) {
                    val m = arr.getJSONObject(i)
                    if (prov.id != "openrouter") {
                        val id = m.getString("id").removePrefix("models/")   // Gemini отдаёт "models/gemini-..."
                        list.add(ModelItem(id, id, false, Double.NaN))
                        continue
                    }
                    val outs = m.optJSONObject("architecture")?.optJSONArray("output_modalities")
                    if (outs != null && (0 until outs.length()).none { outs.optString(it) == "text" }) continue  // не чат-модель (картинки и т.п.)
                    val pr = m.optJSONObject("pricing") ?: continue
                    val p = pr.optString("prompt", "").toDoubleOrNull() ?: continue
                    val q = pr.optString("completion", "").toDoubleOrNull() ?: continue
                    list.add(ModelItem(m.getString("id"), m.optString("name", ""), p == 0.0 && q == 0.0, p * 1_000_000))
                }
                list.sortWith(compareBy<ModelItem>({ !it.free }, { it.id }))
                if (prov.id == "openrouter") {
                    // Роутер бесплатных моделей: сам выбирает любую свободную :free модель, поэтому закреплён сверху
                    list.removeAll { it.id == "openrouter/free" }
                    list.add(0, ModelItem("openrouter/free", "Free Models Router", true, 0.0))
                }
                runOnUiThread { showModelDialog(list, prov) }
            } catch (e: Exception) {
                runOnUiThread { Toast.makeText(this, "Models: ${e.message} (is VPN on for this app? key correct?)", Toast.LENGTH_LONG).show() }
            }
        }.start()
    }

    private fun label(m: ModelItem): String {
        val price = when {
            m.free -> "free"
            m.perMillion.isNaN() -> ""
            m.perMillion < 0 -> "price varies (router)"
            else -> "$" + String.format(java.util.Locale.US, "%.2f", m.perMillion) + " / 1M input tokens"
        }
        return if (price.isEmpty()) m.id else m.id + "\n" + price
    }

    private fun showModelDialog(all: List<ModelItem>, prov: AiService.Companion.Provider) {
        val pad = (12 * resources.displayMetrics.density).toInt()
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, 0) }
        val isOr = prov.id == "openrouter"   // бесплатные модели и лимиты есть только в списке OpenRouter
        val freeOnly = CheckBox(this).apply { text = "Free only"; isChecked = isOr; visibility = if (isOr) View.VISIBLE else View.GONE }
        val limitsNote = TextView(this).apply {
            text = "openrouter/free = сам берёт любую свободную бесплатную модель. Лимит бесплатных: 20 запросов/мин и 50/день на весь аккаунт (1000/день после пополнения от \$10)."
            textSize = 12f
            visibility = if (isOr) View.VISIBLE else View.GONE
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
            getSharedPreferences("ai", MODE_PRIVATE).edit().putString("model_${prov.id}", id).apply()
            Toast.makeText(this, "Model: $id", Toast.LENGTH_SHORT).show()
            dlg.dismiss()
        }
        refreshList()
        dlg.show()
    }
}
