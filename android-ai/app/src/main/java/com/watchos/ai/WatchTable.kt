package com.watchos.ai

import org.json.JSONArray
import org.json.JSONObject

/**
 * Таблицы для часов. Модель отвечает JSON `{"rows": [[...], ...]}` (первая строка - заголовок), иногда в блоке ```json
 * или обычной markdown-таблицей `| a | b |`. normalize() приводит любой из этих видов к виду, который понимает ядро часов
 * (tables.c): не больше 16 строк и 4 столбцов, ячейка до 46 байт UTF-8.
 */
object WatchTable {
    const val MAX_ROWS = 16
    const val MAX_COLS = 4
    private const val MAX_CELL_BYTES = 46   // на часах ячейка 48 байт с нулём

    /** Компактный JSON для часов или null, если в ответе нет таблицы. */
    fun normalize(answer: String): String? {
        val rows = fromJson(answer) ?: fromMarkdown(answer) ?: return null
        val cols = rows.maxOfOrNull { it.size } ?: 0
        if (rows.isEmpty() || cols == 0) return null
        val out = JSONArray()
        for (r in rows.take(MAX_ROWS)) {
            val row = JSONArray()
            for (c in r.take(MAX_COLS)) row.put(cut(c))
            out.put(row)
        }
        return JSONObject().put("rows", out).toString()
    }

    /** Обрезает строку до maxBytes в UTF-8, не разрезая символ. */
    fun cut(s: String, maxBytes: Int = MAX_CELL_BYTES): String {
        val t = s.trim().replace('\n', ' ')
        if (t.toByteArray(Charsets.UTF_8).size <= maxBytes) return t
        val sb = StringBuilder()
        var bytes = 0
        var i = 0
        while (i < t.length) {
            val cp = t.codePointAt(i)
            val ch = String(Character.toChars(cp))
            val n = ch.toByteArray(Charsets.UTF_8).size
            if (bytes + n > maxBytes - 3) break      // место под "..."
            sb.append(ch)
            bytes += n
            i += Character.charCount(cp)
        }
        return sb.append("...").toString()
    }

    private fun cellText(v: Any?): String = when (v) {
        null, JSONObject.NULL -> ""
        is Double -> if (v == Math.rint(v) && Math.abs(v) < 1e15) v.toLong().toString() else v.toString()
        else -> v.toString()
    }

    private fun fromJson(answer: String): List<List<String>>? {
        val a = answer.indexOfFirst { it == '{' || it == '[' }
        if (a < 0) return null
        val close = if (answer[a] == '{') '}' else ']'
        val b = answer.lastIndexOf(close)
        if (b <= a) return null
        val arr: JSONArray = try {
            val src = answer.substring(a, b + 1)
            if (answer[a] == '{') JSONObject(src).optJSONArray("rows") ?: return null else JSONArray(src)
        } catch (_: Exception) {
            return null
        }
        val rows = ArrayList<List<String>>()
        for (i in 0 until arr.length()) {
            val r = arr.optJSONArray(i) ?: continue
            rows.add((0 until r.length()).map { cellText(r.opt(it)) })
        }
        return rows.ifEmpty { null }
    }

    private val SEPARATOR = Regex("^:?-{2,}:?$")

    private fun fromMarkdown(answer: String): List<List<String>>? {
        val rows = ArrayList<List<String>>()
        for (line in answer.lines()) {
            val t = line.trim()
            if (!t.startsWith("|")) continue
            val cells = t.trim('|').split('|').map { it.trim().replace("**", "") }
            if (cells.all { SEPARATOR.matches(it) }) continue     // строка |---|---|
            rows.add(cells)
        }
        return rows.ifEmpty { null }
    }
}
