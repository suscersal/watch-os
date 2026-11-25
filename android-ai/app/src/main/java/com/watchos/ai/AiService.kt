package com.watchos.ai

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.ServerSocket
import java.net.Socket
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors

/**
 * Мост ИИ для часов (docs/ai-bridge.md): на порту 8787 принимает POST /ask {"q":"..."} от часов по сети раздачи
 * и отвечает обычным текстом. Сам идёт в OpenRouter; ключ хранится только в настройках этого приложения.
 * Для часов: 200 + текст ответа, иначе 4xx/5xx + текст ошибки.
 */
class AiService : Service() {
    private var server: ServerSocket? = null
    private var wake: PowerManager.WakeLock? = null
    private val pool = Executors.newCachedThreadPool()
    private val apiLock = Any()      // к OpenRouter идёт один запрос за раз

    companion object {
        const val PORT = 8787
        const val DEFAULT_MODEL = "openai/gpt-4o-mini"
        private const val CHANNEL = "ai_bridge"
        const val DEFAULT_SYSTEM = "You answer on a tiny smartwatch screen (about 20 characters per line). " +
            "Reply in the language of the question. Be brief: at most 300 characters. " +
            "No markdown (no **, #, tables). Write all math in LaTeX: inline \\( ... \\) or \$...\$, " +
            "writing every fraction as \\frac{a}{b} (never a/b), using \\sqrt{x}, \\sqrt[3]{x}, x^{2}, a_{n}, \\pi, \\leq, \\geq, \\neq, \\approx, \\infty, " +
            "\\pm, \\times, \\cdot, \\sum, \\int, Greek letters. Keep formulas short; one calculation step per line. " +
            "For a graph of a function y=f(x) reply with exactly one line 'PLOT: f(x); xmin; xmax' in plain syntax " +
            "(sin(x), x^2, sqrt(x), abs(x), ln(x), pi, no LaTeX) and then one short sentence. " +
            "In PLOT lines xmin and xmax must be plain decimal numbers like -6.28; 6.28, never pi or expressions. " +
            "For other charts or tables give the data as up to 8 lines 'label: value' " +
            "(numbers only after the colon) and then one short sentence."
        const val IMAGE_SYSTEM = "You see a photo on a tiny smartwatch screen. Answer the user's question about it " +
            "in the language of the question. Be brief: at most 400 characters, plain text, no markdown."
        const val TABLE_SYSTEM = "You fill a table for a smartwatch screen. Reply with ONLY JSON, no markdown, no code fences: " +
            "{\"rows\": [[\"Header 1\", \"Header 2\"], [\"a\", \"b\"]]}. The first row is the header. At most 4 columns and 12 rows " +
            "including the header; every cell is plain text of at most 20 characters (no LaTeX). Use the language of the question."
        private const val MAX_IMAGE_ANSWER_BYTES = 900
        private const val MAX_BODY_BYTES = 1_500_000   // фото в base64 до ~350 КБ
        private const val MAX_ANSWER_BYTES = 480   // часы хранят ответ в 512 байтах (VAR_VAL)
        @Volatile var running = false
        @Volatile var info = ""
    }

    override fun onBind(i: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "AI bridge", NotificationManager.IMPORTANCE_LOW))
        val n = Notification.Builder(this, CHANNEL)
            .setContentTitle("Watch AI is running")
            .setContentText("Answering the watch on port $PORT")
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        else startForeground(1, n)

        if (wake == null) {
            wake = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "watchai:bridge")
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
                val ss = ServerSocket(PORT, 8)
                server = ss
                running = true
                info = "Listening on port $PORT"
                while (!ss.isClosed) {
                    val s = ss.accept()
                    pool.execute { handle(s) }
                }
            } catch (e: Exception) {
                if (server != null) info = "Server stopped: ${e.message}"
            } finally {
                running = false
                server = null
            }
        }.start()
    }

    // ---------- HTTP ----------

    private fun handle(s: Socket) {
        s.use {
            try {
                s.soTimeout = 20000
                val req = readRequest(s.getInputStream())
                val out = s.getOutputStream()
                when {
                    req == null -> reply(out, 400, "Bad request")
                    req.first.startsWith("GET /ping") -> reply(out, 200, "ok")
                    !req.first.startsWith("POST /ask") -> reply(out, 404, "Not found")
                    else -> {
                        val j = try { JSONObject(String(req.second, Charsets.UTF_8)) } catch (_: Exception) { null }
                        val img = j?.optString("img").orEmpty().ifEmpty { null }
                        val table = j?.optBoolean("table", false) == true
                        val q = j?.optString("q").orEmpty().ifBlank { if (img != null) "What is in this photo?" else "" }
                        if (q.isBlank()) { reply(out, 400, "Empty question"); return }
                        info = "${now()} asking: ${q.take(40)}"
                        val raw = ask(q.trim(), img, table)
                        val answer = when {
                            img != null -> MathFormat.limitBytes(raw.trim(), MAX_IMAGE_ANSWER_BYTES)
                            table -> WatchTable.normalize(raw) ?: throw ApiError(502, "AI did not return a table")
                            else -> MathFormat.limitBytes(MathFormat.format(Plot.process(raw)), MAX_ANSWER_BYTES)
                        }
                        info = "${now()} OK: ${q.take(40)}"
                        reply(out, 200, answer)
                    }
                }
            } catch (e: ApiError) {
                info = "${now()} error: ${e.message}"
                try { reply(s.getOutputStream(), e.code, e.message ?: "Error") } catch (_: Exception) {}
            } catch (e: Exception) {
                info = "${now()} error: ${e.message}"
                try { reply(s.getOutputStream(), 502, "Phone error: ${e.message}") } catch (_: Exception) {}
            }
        }
    }

    private fun now() = SimpleDateFormat("HH:mm:ss", Locale.US).format(Date())

    /** Возвращает строку запроса и тело, либо null, если запрос не разобрался. */
    private fun readRequest(input: InputStream): Pair<String, ByteArray>? {
        val head = ByteArrayOutputStream()
        var last = 0
        while (head.size() < 8192) {
            val b = input.read()
            if (b < 0) return null
            head.write(b)
            last = (last shl 8) or b
            if (last == 0x0D0A0D0A) break
        }
        if (last != 0x0D0A0D0A) return null
        val lines = head.toString("ISO-8859-1").split("\r\n")
        var len = 0
        for (l in lines.drop(1)) if (l.startsWith("Content-Length:", ignoreCase = true)) len = l.substringAfter(':').trim().toIntOrNull() ?: -1
        if (len < 0 || len > MAX_BODY_BYTES) return null
        val body = ByteArray(len)
        var off = 0
        while (off < len) {
            val r = input.read(body, off, len - off)
            if (r < 0) return null
            off += r
        }
        return lines[0] to body
    }

    private fun reply(out: java.io.OutputStream, code: Int, text: String) {
        val b = text.toByteArray(Charsets.UTF_8)
        val reason = if (code == 200) "OK" else "Error"
        out.write("HTTP/1.1 $code $reason\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: ${b.size}\r\nConnection: close\r\n\r\n"
            .toByteArray(Charsets.ISO_8859_1))
        out.write(b)
        out.flush()
    }

    // ---------- OpenRouter ----------

    private class ApiError(val code: Int, msg: String, val retry: Boolean = false) : Exception(msg)

    /** Бесплатные модели иногда отвечают пустотой или падают на стороне провайдера: пробуем ещё раз (до 3 попыток, ~35 с). */
    private fun ask(q: String, img: String? = null, table: Boolean = false): String {
        val t0 = System.currentTimeMillis()
        var attempt = 1
        while (true) {
            try {
                return askOnce(q, img, table)
            } catch (e: ApiError) {
                if (!e.retry || attempt >= 3 || System.currentTimeMillis() - t0 > 35000) throw e
                attempt++
                Thread.sleep(700)
            }
        }
    }

    private fun askOnce(q: String, img: String?, table: Boolean): String {
        val prefs = getSharedPreferences("ai", MODE_PRIVATE)
        val key = prefs.getString("key", "")?.trim().orEmpty()
        val model = prefs.getString("model", DEFAULT_MODEL)?.trim().takeUnless { it.isNullOrEmpty() } ?: DEFAULT_MODEL
        val system = when {
            img != null -> IMAGE_SYSTEM
            table -> TABLE_SYSTEM
            else -> prefs.getString("system", "")?.trim().takeUnless { it.isNullOrEmpty() } ?: DEFAULT_SYSTEM
        }
        val userContent: Any = if (img == null) q else JSONArray()
            .put(JSONObject().put("type", "text").put("text", q))
            .put(JSONObject().put("type", "image_url").put("image_url", JSONObject().put("url", "data:image/jpeg;base64,$img")))
        if (key.isEmpty()) throw ApiError(503, "No API key in Watch AI app")

        val body = JSONObject()
            .put("model", model)
            .put("max_tokens", 800)   // запас: «думающие» модели тратят часть токенов на рассуждение
            .put("messages", JSONArray()
                .put(JSONObject().put("role", "system").put("content", system))
                .put(JSONObject().put("role", "user").put("content", userContent)))
            .toString()

        synchronized(apiLock) {
            val c = URL("https://openrouter.ai/api/v1/chat/completions").openConnection() as HttpURLConnection
            try {
                c.requestMethod = "POST"
                c.connectTimeout = 10000
                c.readTimeout = 45000
                c.doOutput = true
                c.setRequestProperty("Authorization", "Bearer $key")
                c.setRequestProperty("Content-Type", "application/json")
                c.setRequestProperty("X-Title", "watch-os")
                c.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
                val code = c.responseCode
                val text = (if (code in 200..299) c.inputStream else c.errorStream)
                    ?.bufferedReader(Charsets.UTF_8)?.use { it.readText() }.orEmpty()
                if (code !in 200..299) {
                    val msg = try { JSONObject(text).getJSONObject("error").getString("message") } catch (_: Exception) { text.take(80) }
                    throw ApiError(502, if (code == 429) "Rate limit (free models: 50/day). Wait or change model" else "OpenRouter $code: ${msg.take(70)}", retry = code in 500..599)
                }
                val root = JSONObject(text)
                val choices = root.optJSONArray("choices")
                if (choices == null || choices.length() == 0) {      // 200, но внутри ошибка провайдера
                    val em = root.optJSONObject("error")?.optString("message").orEmpty()
                    throw ApiError(502, "Provider error: ${em.take(60).ifEmpty { "no answer" }}", retry = true)
                }
                val msg = choices.getJSONObject(0).getJSONObject("message")
                val content = if (msg.isNull("content")) "" else msg.getString("content").trim()
                if (content.isEmpty()) throw ApiError(502, "Empty answer, try another model", retry = true)
                return content
            } finally {
                c.disconnect()
            }
        }
    }
}
