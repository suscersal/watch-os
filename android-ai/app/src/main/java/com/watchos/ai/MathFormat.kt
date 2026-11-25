package com.watchos.ai

/**
 * Превращает ответ модели в текст, который умеют рисовать часы: убирает markdown и LaTeX-обёртки,
 * корни, степени, индексы, дроби и греческие буквы заменяет символами Unicode (они есть в шрифте часов,
 * см. src/watch/fonts/README.md): √(x), x², aₙ, π, ≤ ≥ ≠ ≈ ∞ ± × ÷ ∑ ∫ → ...
 */
object MathFormat {
    private val SUP = mapOf('0' to '⁰', '1' to '¹', '2' to '²', '3' to '³', '4' to '⁴', '5' to '⁵', '6' to '⁶',
        '7' to '⁷', '8' to '⁸', '9' to '⁹', '+' to '⁺', '-' to '⁻', '−' to '⁻', 'n' to 'ⁿ')
    private val SUB = mapOf('0' to '₀', '1' to '₁', '2' to '₂', '3' to '₃', '4' to '₄', '5' to '₅', '6' to '₆',
        '7' to '₇', '8' to '₈', '9' to '₉')

    private val WORDS = mapOf(
        "pi" to "π", "alpha" to "α", "beta" to "β", "gamma" to "γ", "delta" to "δ", "epsilon" to "ε", "theta" to "θ",
        "lambda" to "λ", "mu" to "μ", "sigma" to "σ", "phi" to "φ", "omega" to "ω", "Delta" to "Δ", "Sigma" to "Σ",
        "Omega" to "Ω", "cdot" to "·", "times" to "×", "div" to "÷", "pm" to "±", "leq" to "≤", "le" to "≤",
        "geq" to "≥", "ge" to "≥", "neq" to "≠", "ne" to "≠", "approx" to "≈", "infty" to "∞", "sum" to "∑",
        "int" to "∫", "partial" to "∂", "to" to "→", "rightarrow" to "→", "leftarrow" to "←", "Rightarrow" to "→",
        "left" to "", "right" to "", "quad" to " ", "qquad" to " ", "ldots" to "…", "dots" to "…", "degree" to "°",
    )

    /** Возвращает содержимое группы {..} или один символ, начиная с i; вторым значением - позиция после него. */
    private fun group(s: String, i: Int): Pair<String, Int>? {
        if (i >= s.length) return null
        if (s[i] != '{') return s[i].toString() to i + 1
        var depth = 0
        for (j in i until s.length) {
            if (s[j] == '{') depth++
            else if (s[j] == '}' && --depth == 0) return s.substring(i + 1, j) to j + 1
        }
        return null
    }

    private fun simple(t: String) = t.isNotEmpty() && t.all { it.isLetterOrDigit() || it == '.' }
    private fun wrap(t: String) = if (simple(t)) t else "($t)"

    fun format(input: String): String {
        var s = input.replace("\r", "")
        // markdown
        s = s.replace(Regex("```[a-zA-Z]*\\n?"), "").replace("`", "")
            .replace(Regex("\\*\\*(.+?)\\*\\*"), "$1").replace(Regex("__(.+?)__"), "$1")
            .replace(Regex("(?m)^#{1,6}\\s*"), "").replace(Regex("(?m)^\\s*[*-]\\s+"), "• ")
        // обёртки формул
        s = s.replace("\\(", "").replace("\\)", "").replace("\\[", "").replace("\\]", "").replace("$$", "").replace("$", "")
        s = s.replace("<=", "≤").replace(">=", "≥").replace("!=", "≠")

        val out = StringBuilder()
        var i = 0
        while (i < s.length) {
            val c = s[i]
            when {
                c == '\\' -> {
                    var j = i + 1
                    while (j < s.length && s[j].isLetter()) j++
                    val cmd = s.substring(i + 1, j)
                    when {
                        cmd == "frac" || cmd == "dfrac" -> {
                            val a = group(s, j); val b = a?.let { group(s, it.second) }
                            if (a != null && b != null) { out.append(wrap(format(a.first))).append('/').append(wrap(format(b.first))); i = b.second }
                            else i = j
                        }
                        cmd == "sqrt" -> {
                            var idx = ""
                            var k = j
                            if (k < s.length && s[k] == '[') { val e = s.indexOf(']', k); if (e > 0) { idx = s.substring(k + 1, e); k = e + 1 } }
                            val a = group(s, k)
                            if (a != null) {
                                val inner = format(a.first)
                                out.append(idx.map { SUP[it] ?: it }.joinToString("")).append('√').append(wrap(inner))
                                i = a.second
                            } else i = j
                        }
                        cmd == "text" || cmd == "mathrm" || cmd == "mathbf" || cmd == "textbf" -> {
                            val a = group(s, j)
                            if (a != null) { out.append(a.first); i = a.second } else i = j
                        }
                        cmd.isEmpty() -> {          // \, \; \! \\ и т.п.
                            if (j < s.length && s[j] == '\\') out.append('\n')
                            else if (j < s.length && s[j] in ",;: ") out.append(' ')
                            i = (j + 1).coerceAtMost(s.length)
                        }
                        else -> { out.append(WORDS[cmd] ?: cmd); i = j }
                    }
                }
                c == '^' || c == '_' -> {
                    val table = if (c == '^') SUP else SUB
                    // без скобок: знак и все цифры (x^-3, x^10) или один символ
                    val num = if (i + 1 < s.length && s[i + 1] != '{') Regex("^[+\\-−]?\\d+").find(s.substring(i + 1))?.value else null
                    val a = if (num != null) num to i + 1 + num.length else group(s, i + 1)
                    if (a == null) { out.append(c); i++ }
                    else {
                        val body = format(a.first)
                        if (body.isNotEmpty() && body.all { it in table }) out.append(body.map { table[it]!! }.joinToString(""))
                        else if (body.all { it in "²³¹⁰⁴⁵⁶⁷⁸⁹ⁿ⁺⁻₀₁₂₃₄₅₆₇₈₉" }) out.append(body)
                        else out.append(c).append(if (body.length == 1) body else "($body)")
                        i = a.second
                    }
                }
                c == 's' && s.startsWith("sqrt(", i) && (i == 0 || !s[i - 1].isLetter()) -> { out.append('√'); i += 4 }
                c == '{' || c == '}' -> i++
                else -> { out.append(c); i++ }
            }
        }
        // sqrt(x) -> √(x), x^2 уже обработано выше; лишние пробелы и пустые строки
        return out.toString().replace(Regex("[ \\t]+\\n"), "\n").replace(Regex("(?m)^[ \\t]+"), "").replace(Regex("\\n{3,}"), "\n\n").trim()
    }

    /** Обрезает строку до maxBytes в UTF-8, не разрезая символ, и ставит многоточие. */
    fun limitBytes(s: String, maxBytes: Int): String {
        if (s.toByteArray(Charsets.UTF_8).size <= maxBytes) return s
        var n = s.length
        while (n > 0 && (s.substring(0, n) + "…").toByteArray(Charsets.UTF_8).size > maxBytes) n--
        return s.substring(0, n).trimEnd() + "…"
    }
}
