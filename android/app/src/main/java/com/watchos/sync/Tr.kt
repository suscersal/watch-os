package com.watchos.sync

import java.util.Locale

/** Строки интерфейса: русский при системном языке ru, иначе английский. */
fun tr(en: String, ru: String): String = if (Locale.getDefault().language == "ru") ru else en

/** Шаг, на котором оборвалась операция (для текста ошибки). */
fun stepName(step: String): String {
    if (Locale.getDefault().language != "ru") return step
    return when {
        step == "file" -> "чтение файла"
        step == "usb" -> "USB"
        step == "flash" -> "прошивка"
        step == "check" -> "проверка"
        step == "connect" -> "подключение"
        step == "hello" -> "приветствие"
        step == "firmware" -> "обновление прошивки"
        step == "convert" -> "конвертация"
        step == "pair" -> "сопряжение"
        step == "time" -> "время"
        step == "list" -> "список"
        step.startsWith("track ") -> "трек " + step.removePrefix("track ")
        step.startsWith("photo ") -> "фото " + step.removePrefix("photo ")
        else -> step
    }
}
