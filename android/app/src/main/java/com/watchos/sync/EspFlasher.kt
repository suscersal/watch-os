package com.watchos.sync

import java.io.ByteArrayOutputStream
import java.security.MessageDigest
import java.util.zip.Deflater

// Прошивка ESP32-S3 по USB через ROM-загрузчик (протокол esptool: SLIP, сжатая запись FLASH_DEFL_*).
// Чистый Kotlin без Android: порт (USB CDC) подставляется снаружи, поэтому проверяется на JVM (jvmtest/FlashSelfTest.kt).

interface FlashPort {
    fun write(data: ByteArray)
    /** Читает до buf.size байт, ждёт не дольше timeoutMs; 0 - ничего не пришло. */
    fun read(buf: ByteArray, timeoutMs: Int): Int
    /** Линии DTR/RTS (на USB-Serial-JTAG: IO0 и EN). */
    fun setLines(dtr: Boolean, rts: Boolean)
}

class FlashException(message: String) : Exception(message)

class FlashRegion(val offset: Int, val data: ByteArray)

/** Что писать: из полного образа (watch-os-full.bin) либо всё, либо всё кроме разделов nvs и storage (данные остаются). */
object FlashPlan {
    const val TABLE_OFFSET = 0x8000
    const val SECTOR = 0x1000
    const val FLASH_SIZE = 0x1000000          // 16 МБ

    class Entry(val type: Int, val subtype: Int, val offset: Int, val size: Int, val label: String)

    private fun u8(b: ByteArray, p: Int) = b[p].toInt() and 0xFF
    private fun u32(b: ByteArray, p: Int): Long =
        (u8(b, p).toLong()) or (u8(b, p + 1).toLong() shl 8) or (u8(b, p + 2).toLong() shl 16) or (u8(b, p + 3).toLong() shl 24)

    fun parseTable(img: ByteArray): List<Entry> {
        val out = ArrayList<Entry>()
        var p = TABLE_OFFSET
        while (p + 32 <= img.size && p + 32 <= TABLE_OFFSET + SECTOR) {
            if (u8(img, p) != 0xAA || u8(img, p + 1) != 0x50) break
            val off = u32(img, p + 4)
            val size = u32(img, p + 8)
            if (off > FLASH_SIZE || size > FLASH_SIZE) break
            var n = 0
            while (n < 16 && img[p + 12 + n].toInt() != 0) n++
            out.add(Entry(u8(img, p + 2), u8(img, p + 3), off.toInt(), size.toInt(), String(img, p + 12, n, Charsets.US_ASCII)))
            p += 32
        }
        return out
    }

    fun problem(img: ByteArray): String? {
        if (img.size < 0x20000) return "the file is too small for a full image (choose watch-os-full.bin)"
        if (img.size > FLASH_SIZE) return "the file is bigger than the 16 MB flash"
        if (u8(img, 0) != 0xE9) return "no bootloader at 0x0 (choose watch-os-full.bin, not watch_os.bin)"
        if (parseTable(img).isEmpty()) return "no partition table at 0x8000 (choose watch-os-full.bin)"
        return null
    }

    fun plan(img: ByteArray, keepData: Boolean): List<FlashRegion> {
        problem(img)?.let { throw FlashException(it) }
        if (!keepData) return listOf(FlashRegion(0, img))
        val keep = parseTable(img).filter { it.label == "nvs" || it.label == "storage" }
        if (keep.none { it.label == "storage" }) throw FlashException(tr("the image has no storage partition, so data cannot be kept", "в образе нет раздела storage, данные сохранить нельзя"))
        val skip = keep.map { Pair(it.offset, minOf(it.offset + it.size, img.size)) }.sortedBy { it.first }
        val out = ArrayList<FlashRegion>()
        fun add(from: Int, to: Int) {
            var end = to
            while (end > from && img[end - 1] == 0xFF.toByte()) end--          // хвост из 0xFF не пишем (старое там и так не нужно)
            if (end == from) return
            end = minOf(to, (end + SECTOR - 1) / SECTOR * SECTOR)
            out.add(FlashRegion(from, img.copyOfRange(from, end)))
        }
        var pos = 0
        for (s in skip) {
            if (s.first > pos) add(pos, minOf(s.first, img.size))
            pos = maxOf(pos, s.second)
        }
        if (pos < img.size) add(pos, img.size)
        return out
    }
}

class EspFlasher(private val port: FlashPort, private val log: (String) -> Unit = {}) {
    companion object {
        const val BLOCK = 0x400
        private const val OP_SYNC = 0x08
        private const val OP_READ_REG = 0x0A
        private const val OP_SPI_SET_PARAMS = 0x0B
        private const val OP_SPI_ATTACH = 0x0D
        private const val OP_DEFL_BEGIN = 0x10
        private const val OP_DEFL_DATA = 0x11
        private const val OP_DEFL_END = 0x12
        private const val OP_MD5 = 0x13

        fun slip(p: ByteArray): ByteArray {
            val o = ByteArrayOutputStream(p.size + 16)
            o.write(0xC0)
            for (b in p) {
                when (b.toInt() and 0xFF) {
                    0xC0 -> { o.write(0xDB); o.write(0xDC) }
                    0xDB -> { o.write(0xDB); o.write(0xDD) }
                    else -> o.write(b.toInt())
                }
            }
            o.write(0xC0)
            return o.toByteArray()
        }

        fun le32(vararg v: Int): ByteArray {
            val o = ByteArray(v.size * 4)
            for (i in v.indices) for (k in 0 until 4) o[i * 4 + k] = (v[i] shr (8 * k)).toByte()
            return o
        }

        fun checksum(d: ByteArray): Int {
            var c = 0xEF
            for (b in d) c = c xor (b.toInt() and 0xFF)
            return c
        }

        fun deflate(d: ByteArray): ByteArray {
            val z = Deflater(9)
            z.setInput(d)
            z.finish()
            val o = ByteArrayOutputStream(d.size / 2 + 64)
            val buf = ByteArray(65536)
            while (!z.finished()) o.write(buf, 0, z.deflate(buf))
            z.end()
            return o.toByteArray()
        }
    }

    private val rbuf = ByteArray(1024)
    private var rpos = 0
    private var rlen = 0
    private val cur = ByteArrayOutputStream()
    private var inFrame = false
    private var esc = false

    private fun readFrame(timeoutMs: Int): ByteArray? {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (true) {
            while (rpos < rlen) {
                val b = rbuf[rpos++].toInt() and 0xFF
                if (b == 0xC0) {
                    if (inFrame && cur.size() > 0) {
                        val f = cur.toByteArray()
                        cur.reset(); inFrame = false; esc = false
                        return f
                    }
                    inFrame = true; cur.reset(); esc = false
                } else if (!inFrame) {
                    // мусор до начала кадра (текст ROM-загрузчика)
                } else if (esc) {
                    cur.write(if (b == 0xDC) 0xC0 else if (b == 0xDD) 0xDB else b)
                    esc = false
                } else if (b == 0xDB) {
                    esc = true
                } else {
                    cur.write(b)
                }
            }
            val left = deadline - System.currentTimeMillis()
            if (left <= 0) return null
            rpos = 0
            rlen = port.read(rbuf, minOf(left, 500L).toInt())
        }
    }

    private class Resp(val value: Int, val data: ByteArray)

    private fun command(op: Int, data: ByteArray, chk: Int = 0, timeoutMs: Int = 3000): Resp {
        val pkt = ByteArray(8 + data.size)
        pkt[0] = 0
        pkt[1] = op.toByte()
        pkt[2] = data.size.toByte()
        pkt[3] = (data.size shr 8).toByte()
        System.arraycopy(le32(chk), 0, pkt, 4, 4)
        System.arraycopy(data, 0, pkt, 8, data.size)
        port.write(slip(pkt))
        val deadline = System.currentTimeMillis() + timeoutMs
        while (true) {
            val left = (deadline - System.currentTimeMillis()).toInt()
            if (left <= 0) throw FlashException("no reply to command 0x%02x".format(op))
            val f = readFrame(left) ?: continue
            if (f.size < 10 || f[0].toInt() != 1 || (f[1].toInt() and 0xFF) != op) continue
            val value = (f[4].toInt() and 0xFF) or ((f[5].toInt() and 0xFF) shl 8) or ((f[6].toInt() and 0xFF) shl 16) or ((f[7].toInt() and 0xFF) shl 24)
            val body = f.copyOfRange(8, f.size)
            val st = body.size - 2
            if (body[st].toInt() != 0) throw FlashException("command 0x%02x failed (error 0x%02x)".format(op, body[st + 1].toInt() and 0xFF))
            return Resp(value, body.copyOfRange(0, st))
        }
    }

    private fun resetToBootloader() {
        // последовательность esptool для USB-Serial-JTAG: IO0 в ноль, затем сброс, затем отпустить
        port.setLines(dtr = false, rts = false); Thread.sleep(100)
        port.setLines(dtr = true, rts = false); Thread.sleep(100)
        port.setLines(dtr = false, rts = true)
        port.setLines(dtr = false, rts = true); Thread.sleep(100)
        port.setLines(dtr = false, rts = false)
        Thread.sleep(200)
    }

    private fun trySync(): Boolean {
        val d = ByteArray(36)
        d[0] = 7; d[1] = 7; d[2] = 0x12; d[3] = 0x20
        for (i in 4 until 36) d[i] = 0x55
        for (i in 0 until 8) {
            try {
                    command(OP_SYNC, d, 0, 200)
                    while (readFrame(100) != null) { /* ROM отвечает на SYNC несколько раз: вычитываем хвост */ }
                    return true
                } catch (_: FlashException) {
                }
            }
            return false
        }

        fun connect() {
            var ok = false
            for (attempt in 1..6) {
                log("Resetting the watch into download mode ($attempt/6)")
                resetToBootloader()
                rpos = 0; rlen = 0; cur.reset(); inFrame = false; esc = false
                if (trySync()) { ok = true; break }
            }
            if (!ok) throw FlashException(tr("The watch does not answer. Hold the BOOT button while plugging the USB cable, then try again.", "Часы не отвечают. Зажмите кнопку BOOT при подключении USB-кабеля и повторите."))
            try {
                val magic = command(OP_READ_REG, le32(0x40001000)).value
                log("Chip id 0x%x%s".format(magic, if (magic == 9) " (ESP32-S3)" else " (unexpected)"))
            } catch (e: FlashException) {
                log("Chip id not read: ${e.message}")
            }
            command(OP_SPI_ATTACH, ByteArray(8))
            command(OP_SPI_SET_PARAMS, le32(0, FlashPlan.FLASH_SIZE, 0x10000, 0x1000, 0x100, 0xFFFF))
        }

        private fun hex(b: ByteArray): String = b.joinToString("") { "%02x".format(it.toInt() and 0xFF) }

        /** Пишет области, проверяет MD5 каждой, затем перезагружает часы. progress: 0..100. */
        fun write(regions: List<FlashRegion>, progress: (Int) -> Unit = {}) {
            val comps = regions.map { deflate(it.data) }
            val total = maxOf(1, comps.sumOf { (it.size + BLOCK - 1) / BLOCK })
            var done = 0
            // Загрузчик и таблица разделов (адрес 0) пишем последними: если запись оборвётся на приложении,
            // старый загрузчик и старая таблица останутся, и часы продолжат загружаться.
            val order = regions.indices.sortedBy { if (regions[it].offset == 0) 1 else 0 }
            for (i in order) {
                val r = regions[i]
                val c = comps[i]
                try {
                val blocks = (c.size + BLOCK - 1) / BLOCK
                log("Writing 0x%x, %d KB (erasing first)".format(r.offset, r.data.size / 1024))
                command(OP_DEFL_BEGIN, le32(r.data.size, blocks, BLOCK, r.offset, 0), 0,
                    maxOf(3000L, 30000L * r.data.size / 1000000L).toInt())
                for (seq in 0 until blocks) {
                    val blk = c.copyOfRange(seq * BLOCK, minOf(c.size, (seq + 1) * BLOCK))
                    command(OP_DEFL_DATA, le32(blk.size, seq, 0, 0) + blk, checksum(blk), 5000)
                    done++
                    progress(done * 100 / total)
                }
                val md5 = command(OP_MD5, le32(r.offset, r.data.size, 0, 0), 0,
                    maxOf(3000L, 8000L * r.data.size / 1000000L).toInt()).data
                val got = if (md5.size >= 32) String(md5, 0, 32, Charsets.US_ASCII).lowercase() else hex(md5.copyOf(minOf(16, md5.size)))
                val want = hex(MessageDigest.getInstance("MD5").digest(r.data))
                if (got != want) throw FlashException("check failed at 0x%x: the flash holds different data than was written".format(r.offset))
            } catch (e: FlashException) {
                throw FlashException((e.message ?: "flash error") + " [0x%x, %d KB]".format(r.offset, r.data.size / 1024))
            }
        }
        try {
            command(OP_DEFL_END, le32(1), 0, 3000)
        } catch (_: FlashException) {
        }
        log("Restarting the watch")
        port.setLines(dtr = false, rts = true); Thread.sleep(100)
        port.setLines(dtr = false, rts = false)
    }
}
