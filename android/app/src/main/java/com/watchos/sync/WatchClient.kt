package com.watchos.sync

// Клиент протокола синхронизации часов (версия 1), см. docs/sync-protocol.md.
// Чистый Kotlin/JVM, без Android: поэтому его можно проверить обычной JVM (android/jvmtest).

import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.Closeable
import java.io.DataInputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

class WatchException(message: String) : Exception(message)

data class WatchInfo(val photoCount: Int, val maxPhotos: Int, val maxFullBytes: Long)

object Proto {
    const val VERSION = 1
    const val T_HELLO = 0x01
    const val T_TIME = 0x02
    const val T_PHOTO_BEGIN = 0x10
    const val T_PHOTO_DATA = 0x11
    const val T_PHOTO_END = 0x12
    const val T_PHOTO_LIST = 0x13
    const val T_PHOTO_DELETE = 0x14
    const val T_MESSAGE = 0x20
    const val T_BYE = 0x7F
    const val MSG_APP_MAX = 32
    const val MSG_TITLE_MAX = 64
    const val MSG_TEXT_MAX = 256
    const val R_OK = 0x80
    const val R_HELLO_OK = 0x81
    const val R_ERR = 0x8F
    const val R_LIST = 0x91
    const val CHUNK = 16384
    const val MAX_FRAME = 20000

    fun errorText(code: Int): String = when (code) {
        1 -> "bad frame"
        2 -> "wrong code"
        3 -> "wrong state"
        4 -> "too big"
        5 -> "checksum mismatch"
        6 -> "image format not supported by the watch"
        7 -> "watch storage is full"
        8 -> "not supported"
        9 -> "watch is locked"
        10 -> "not allowed with this code"
        else -> "error $code"
    }
}

/** Обрезает строку до maxBytes в utf-8, не разрывая символ (в том числе суррогатную пару). */
fun utf8Cut(s: String, maxBytes: Int): ByteArray {
    val full = s.toByteArray(Charsets.UTF_8)
    if (full.size <= maxBytes) return full
    var n = maxBytes
    while (n > 0 && (full[n].toInt() and 0xC0) == 0x80) n--   // full[n] - продолжение символа: отступаем к его началу
    return full.copyOf(n)
}

/**
 * Часы показывают JPEG только с заголовком JFIF (требование декодера LVGL). Некоторые кодировщики его не пишут,
 * поэтому при необходимости вставляем стандартный сегмент APP0 сразу после SOI.
 */
fun ensureJfif(jpeg: ByteArray): ByteArray {
    require(jpeg.size > 4 && jpeg[0] == 0xFF.toByte() && jpeg[1] == 0xD8.toByte()) { "not a JPEG" }
    val jfif = intArrayOf(
        0xFF, 0xE0, 0x00, 0x10, 'J'.code, 'F'.code, 'I'.code, 'F'.code, 0x00,
        0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00
    ).map { it.toByte() }.toByteArray()
    val has = jpeg.size >= 18 && jpeg[2] == jfif[0] && jpeg[3] == jfif[1] && jpeg[4] == jfif[2] && jpeg[5] == jfif[3] &&
        jpeg[6] == jfif[4] && jpeg[7] == jfif[5] && jpeg[8] == jfif[6] && jpeg[9] == jfif[7]
    if (has) return jpeg
    return byteArrayOf(jpeg[0], jpeg[1]) + jfif + jpeg.copyOfRange(2, jpeg.size)
}

class WatchClient(private val host: String, private val port: Int) : Closeable {
    private val socket = Socket()
    private lateinit var input: DataInputStream
    private lateinit var output: OutputStream

    fun connect(timeoutMs: Int = 8000) {
        socket.connect(InetSocketAddress(host, port), timeoutMs)
        socket.soTimeout = 20000
        socket.tcpNoDelay = true
        input = DataInputStream(BufferedInputStream(socket.getInputStream()))
        output = BufferedOutputStream(socket.getOutputStream())
    }

    private fun send(type: Int, payload: ByteArray = ByteArray(0)) {
        val head = ByteBuffer.allocate(5).order(ByteOrder.LITTLE_ENDIAN)
        head.put(type.toByte()).putInt(payload.size)
        output.write(head.array())
        output.write(payload)
        output.flush()
    }

    private fun reply(): Pair<Int, ByteArray> {
        val type = input.readUnsignedByte()
        val lenBytes = ByteArray(4)
        input.readFully(lenBytes)
        val len = ByteBuffer.wrap(lenBytes).order(ByteOrder.LITTLE_ENDIAN).int
        if (len < 0 || len > Proto.MAX_FRAME) throw WatchException("bad reply length")
        val payload = ByteArray(len)
        input.readFully(payload)
        if (type == Proto.R_ERR) {
            val code = if (payload.size >= 2) payload[1].toInt() and 0xFF else 0
            throw WatchException(Proto.errorText(code))
        }
        return type to payload
    }

    fun hello(code: String, deviceName: String = "phone"): WatchInfo {
        require(code.length == 6 && code.all { it in '0'..'9' }) { "code must be 6 digits" }
        val name = deviceName.toByteArray(Charsets.UTF_8).let { if (it.size > 32) it.copyOf(32) else it }
        send(Proto.T_HELLO, byteArrayOf(Proto.VERSION.toByte()) + code.toByteArray(Charsets.US_ASCII) + name)
        val (_, p) = reply()
        val b = ByteBuffer.wrap(p).order(ByteOrder.LITTLE_ENDIAN)
        b.get()                                   // версия протокола
        val count = b.short.toInt() and 0xFFFF
        val max = b.short.toInt() and 0xFFFF
        val maxBytes = b.int.toLong() and 0xFFFFFFFFL
        return WatchInfo(count, max, maxBytes)
    }

    fun setTime(epochSeconds: Long, tzMinutes: Int) {
        val b = ByteBuffer.allocate(10).order(ByteOrder.LITTLE_ENDIAN)
        b.putLong(epochSeconds).putShort(tzMinutes.toShort())
        send(Proto.T_TIME, b.array())
        reply()
    }

    /** Уведомление (кадр MESSAGE). id один и тот же у обновлённого уведомления: часы заменят прежнее. */
    fun sendMessage(id: Int, epochSeconds: Long, app: String, title: String, text: String) {
        val a = utf8Cut(app, Proto.MSG_APP_MAX)
        val t = utf8Cut(title, Proto.MSG_TITLE_MAX)
        val x = utf8Cut(text, Proto.MSG_TEXT_MAX)
        val b = ByteBuffer.allocate(12 + a.size + t.size + x.size).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(id).putInt(epochSeconds.toInt()).put(a.size.toByte()).put(t.size.toByte()).putShort(x.size.toShort())
        b.put(a).put(t).put(x)
        send(Proto.T_MESSAGE, b.array())
        reply()
    }

    fun listPhotos(): Set<Long> {
        send(Proto.T_PHOTO_LIST)
        val (_, p) = reply()
        val b = ByteBuffer.wrap(p).order(ByteOrder.LITTLE_ENDIAN)
        val n = b.short.toInt() and 0xFFFF
        return (0 until n).map { b.int.toLong() and 0xFFFFFFFFL }.toSet()
    }

    fun deletePhoto(id: Long) {
        send(Proto.T_PHOTO_DELETE, ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(id.toInt()).array())
        reply()
    }

    /** true - фото передано, false - такое уже есть на часах. */
    fun sendPhoto(id: Long, takenSeconds: Long, thumb: ByteArray, full: ByteArray, onProgress: (Int, Int) -> Unit = { _, _ -> }): Boolean {
        val crc = CRC32().apply { update(thumb); update(full) }.value
        val begin = ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN)
        begin.putInt(id.toInt()).putInt(takenSeconds.toInt()).putInt(thumb.size).putInt(full.size).putInt(crc.toInt())
        send(Proto.T_PHOTO_BEGIN, begin.array())
        val (_, ok) = reply()
        if (ok.size >= 2 && ok[1].toInt() == 1) return false
        val data = thumb + full
        var sent = 0
        while (sent < data.size) {
            val n = minOf(Proto.CHUNK, data.size - sent)
            send(Proto.T_PHOTO_DATA, data.copyOfRange(sent, sent + n))
            sent += n
            onProgress(sent, data.size)
        }
        send(Proto.T_PHOTO_END)
        reply()
        return true
    }

    override fun close() {
        try { send(Proto.T_BYE) } catch (_: Exception) { }
        try { socket.close() } catch (_: Exception) { }
    }
}
