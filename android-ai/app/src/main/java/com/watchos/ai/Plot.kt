package com.watchos.ai

import kotlin.math.*

/**
 * Графики для часов. Модель не рисует: на запрос графика она отвечает строкой `PLOT: f(x); xmin; xmax`,
 * телефон считает ~40 точек и заменяет строку маркером для часов (виджет math рисует его как lv_chart):
 * PL_A xmin|xmax|ymin|ymax|v,v,... PL_C, где v - целые 0..100 (масштаб по y), x - точка не определена.
 */
object Plot {
    const val PL_A = '\u0004'
    const val PL_C = '\u0005'
    private const val POINTS = 40
    private val LINE = Regex("(?mi)^\\s*PLOT:\\s*(.+?)\\s*;\\s*(-?[\\d.]+)\\s*;\\s*(-?[\\d.]+)\\s*$")

    fun process(answer: String): String = answer.replace(LINE) { m ->
        try { build(m.groupValues[1], m.groupValues[2].toDouble(), m.groupValues[3].toDouble()) } catch (_: Exception) { "" }
    }

    private fun fmt(d: Double): String {
        val r = (d * 100).roundToInt() / 100.0
        return if (r == r.toLong().toDouble()) r.toLong().toString() else r.toString()
    }

    private fun build(expr: String, x0: Double, x1: Double): String {
        require(x1 > x0)
        val e = Expr(expr)
        val ys = DoubleArray(POINTS) { i -> e.eval(x0 + (x1 - x0) * i / (POINTS - 1)) }
        val ok = ys.filter { it.isFinite() && abs(it) < 1e9 }
        require(ok.size >= 2)
        var lo = ok.min(); var hi = ok.max()
        if (hi - lo < 1e-9) { lo -= 1; hi += 1 }
        val vals = ys.joinToString(",") { y -> if (y.isFinite() && abs(y) < 1e9) (((y - lo) / (hi - lo)) * 100).roundToInt().toString() else "x" }
        return "$PL_A${fmt(x0)}|${fmt(x1)}|${fmt(lo)}|${fmt(hi)}|$vals$PL_C"
    }

    /** Маленький вычислитель: + - * / ^, скобки, неявное умножение (2x, 2sin(x)), sin cos tan asin acos atan sqrt abs ln log exp, pi, e, x. */
    private class Expr(src: String) {
        private val s = src.replace("\\", "").replace('{', '(').replace('}', ')').replace("·", "*").replace("×", "*")
            .replace("π", "pi").replace("−", "-").replace("²", "^2").replace("³", "^3").replace(" ", "").lowercase()
            .replace(Regex("^(y|f\\(x\\))="), "")
        private var pos = 0
        private var x = 0.0

        fun eval(xv: Double): Double {
            x = xv; pos = 0
            val v = sum()
            require(pos == s.length) { "junk" }
            return v
        }

        private fun peek() = if (pos < s.length) s[pos] else '\u0000'

        private fun sum(): Double {
            var v = term()
            while (peek() == '+' || peek() == '-') { val op = s[pos++]; val r = term(); v = if (op == '+') v + r else v - r }
            return v
        }

        private fun term(): Double {
            var v = unary()
            while (true) {
                val c = peek()
                if (c == '*') { pos++; v *= unary() }
                else if (c == '/') { pos++; v /= unary() }
                else if (c.isLetter() || c == '(' || c.isDigit() || c == '.') v *= power()   // неявное умножение
                else return v
            }
        }

        private fun unary(): Double {
            if (peek() == '-') { pos++; return -unary() }
            if (peek() == '+') { pos++; return unary() }
            return power()
        }

        private fun power(): Double {
            val b = atom()
            if (peek() == '^') { pos++; return b.pow(unary()) }
            return b
        }

        private fun atom(): Double {
            val c = peek()
            if (c == '(') { pos++; val v = sum(); require(peek() == ')'); pos++; return v }
            if (c.isDigit() || c == '.') {
                val st = pos
                while (peek().isDigit() || peek() == '.') pos++
                return s.substring(st, pos).toDouble()
            }
            require(c.isLetter())
            val st = pos
            while (peek().isLetter()) pos++
            val name = s.substring(st, pos)
            when (name) { "x" -> return x; "pi" -> return PI; "e" -> return E }
            require(peek() == '(')
            pos++; val a = sum(); require(peek() == ')'); pos++
            return when (name) {
                "sin" -> sin(a); "cos" -> cos(a); "tan" -> tan(a)
                "asin", "arcsin" -> asin(a); "acos", "arccos" -> acos(a); "atan", "arctan" -> atan(a)
                "sqrt" -> sqrt(a); "abs" -> abs(a); "ln" -> ln(a); "log" -> log10(a); "exp" -> exp(a)
                else -> throw IllegalArgumentException(name)
            }
        }
    }
}
