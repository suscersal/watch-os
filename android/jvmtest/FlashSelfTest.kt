// Проверка EspFlasher/FlashPlan на обычной JVM против поддельного ROM-загрузчика ESP32-S3 (SLIP, FLASH_DEFL_*, MD5).
// Запуск: python3 tests/run_flash_selftest.py <kotlinc>
import com.watchos.sync.EspFlasher
import com.watchos.sync.FlashException
import com.watchos.sync.FlashPlan
import com.watchos.sync.FlashPort
import java.io.ByteArrayOutputStream
import java.security.MessageDigest
import java.util.Random
import java.util.zip.Inflater
import kotlin.system.exitProcess

var failures = 0
fun check(ok: Boolean, msg: String) { println((if (ok) "ok:   " else "FAIL: ") + msg); if (!ok) failures++ }

class FakeRom(val flash: ByteArray, val corrupt: Boolean = false) : FlashPort {
    private val out = ArrayDeque<Byte>()
    private val cur = ByteArrayOutputStream()
    private var inFrame = false
    private var esc = false
    var begin = 0; var size = 0; var blocks = 0; var got = 0
    val comp = ByteArrayOutputStream()
    var resets = 0

    private fun u32(b: ByteArray, p: Int) = (b[p].toInt() and 0xFF) or ((b[p + 1].toInt() and 0xFF) shl 8) or ((b[p + 2].toInt() and 0xFF) shl 16) or ((b[p + 3].toInt() and 0xFF) shl 24)

    override fun setLines(dtr: Boolean, rts: Boolean) { if (rts) resets++ }

    override fun write(data: ByteArray) {
        for (x in data) {
            val b = x.toInt() and 0xFF
            if (b == 0xC0) {
                if (inFrame && cur.size() > 0) { handle(cur.toByteArray()); cur.reset() }
                inFrame = true; esc = false; cur.reset()
            } else if (!inFrame) {
            } else if (esc) { cur.write(if (b == 0xDC) 0xC0 else 0xDB); esc = false }
            else if (b == 0xDB) esc = true
            else cur.write(b)
        }
    }

    override fun read(buf: ByteArray, timeoutMs: Int): Int {
        var n = 0
        while (n < buf.size && out.isNotEmpty()) buf[n++] = out.removeFirst()
        return n
    }

    private fun reply(op: Int, value: Int, data: ByteArray, err: Int = 0) {
        // Как настоящий ROM ESP32-S3: после данных идут 2 байта статуса и 2 зарезервированных, в которых бывает мусор.
        val p = ByteArray(8 + data.size + 4)
        p[0] = 1; p[1] = op.toByte(); p[2] = (data.size + 4).toByte(); p[3] = ((data.size + 4) shr 8).toByte()
        System.arraycopy(EspFlasher.le32(value), 0, p, 4, 4)
        System.arraycopy(data, 0, p, 8, data.size)
        p[8 + data.size] = if (err != 0) 1 else 0; p[9 + data.size] = err.toByte()
        p[10 + data.size] = 0x63; p[11 + data.size] = 0x66
        for (b in EspFlasher.slip(p)) out.addLast(b)
    }

    private fun handle(f: ByteArray) {
        val op = f[1].toInt() and 0xFF
        val len = (f[2].toInt() and 0xFF) or ((f[3].toInt() and 0xFF) shl 8)
        val chk = u32(f, 4)
        val d = f.copyOfRange(8, 8 + len)
        when (op) {
            0x08 -> { reply(op, 0, ByteArray(0)); reply(op, 0, ByteArray(0)) }
            0x0A -> reply(op, 9, ByteArray(0))
            0x0B, 0x0D -> reply(op, 0, ByteArray(0))
            0x10 -> {
                size = u32(d, 0); blocks = u32(d, 4); begin = u32(d, 12); got = 0; comp.reset()
                java.util.Arrays.fill(flash, begin, begin + size, 0xFF.toByte())
                reply(op, 0, ByteArray(0))
            }
            0x11 -> {
                val n = u32(d, 0); val payload = d.copyOfRange(16, 16 + n)
                if (EspFlasher.checksum(payload) != chk) { reply(op, 0, ByteArray(0), 0x07); return }
                comp.write(payload); got++
                if (got == blocks) {
                    val z = Inflater(); z.setInput(comp.toByteArray())
                    val raw = ByteArray(size); var o = 0
                    while (o < size && !z.finished()) o += z.inflate(raw, o, size - o)
                    z.end()
                    if (o != size) { reply(op, 0, ByteArray(0), 0x05); return }
                    System.arraycopy(raw, 0, flash, begin, size)
                    if (corrupt) flash[begin + 5] = (flash[begin + 5].toInt() xor 1).toByte()
                }
                reply(op, 0, ByteArray(0))
            }
            0x12 -> reply(op, 0, ByteArray(0))
            0x13 -> {
                val a = u32(d, 0); val s = u32(d, 4)
                val md = MessageDigest.getInstance("MD5").digest(flash.copyOfRange(a, a + s))
                reply(op, 0, md.joinToString("") { "%02x".format(it.toInt() and 0xFF) }.toByteArray())
            }
            else -> reply(op, 0, ByteArray(0), 0x08)
        }
    }
}

fun main() {
    val rnd = Random(42)
    fun rand(n: Int) = ByteArray(n).also { rnd.nextBytes(it) }
    val total = 0x810000 + 0x700000
    val img = ByteArray(total) { 0xFF.toByte() }
    fun put(off: Int, d: ByteArray) = System.arraycopy(d, 0, img, off, d.size)
    put(0, byteArrayOf(0xE9.toByte()) + rand(20000))
    fun ent(type: Int, sub: Int, off: Int, size: Int, name: String): ByteArray {
        val e = ByteArray(32); e[0] = 0xAA.toByte(); e[1] = 0x50; e[2] = type.toByte(); e[3] = sub.toByte()
        System.arraycopy(EspFlasher.le32(off, size), 0, e, 4, 8)
        for ((i, c) in name.withIndex()) e[12 + i] = c.code.toByte()
        return e
    }
    var tp = 0x8000
    for (e in listOf(ent(1, 2, 0x9000, 0x4000, "nvs"), ent(1, 0, 0xd000, 0x2000, "otadata"), ent(1, 1, 0xf000, 0x1000, "phy_init"),
        ent(0, 0x10, 0x10000, 0x400000, "ota_0"), ent(0, 0x11, 0x410000, 0x400000, "ota_1"), ent(1, 0x82, 0x810000, 0x700000, "storage"))) { put(tp, e); tp += 32 }
    put(0xf000, rand(4096))
    val appLen = 300000
    put(0x10000, rand(appLen))
    put(0x810000, rand(100000))
    val appEnd = (0x10000 + appLen + 0xFFF) / 0x1000 * 0x1000

    val plan = FlashPlan.plan(img, true)
    check(plan.map { it.offset } == listOf(0, 0xd000), "keep-data plan: regions at 0x0 and 0xd000 (got ${plan.map { "0x%x".format(it.offset) }})")
    check(plan[0].data.size == 0x9000 - 0 || plan[0].data.size <= 0x9000, "first region stays below nvs")
    check(plan[1].offset + plan[1].data.size == appEnd, "second region ends after the app, trailing 0xFF trimmed")

    // --- сохранить данные
    val before = rand(total)
    val flash = before.copyOf()
    val rom = FakeRom(flash)
    var last = 0
    val ef = EspFlasher(rom) { println("  log: $it") }
    ef.connect()
    ef.write(plan) { last = it }
    check(last == 100, "progress reaches 100")
    check(flash.copyOfRange(0, 0x9000).contentEquals(img.copyOfRange(0, 0x9000)), "bootloader and partition table written")
    check(flash.copyOfRange(0xd000, appEnd).contentEquals(img.copyOfRange(0xd000, appEnd)), "otadata, phy and app written")
    check(flash.copyOfRange(0x9000, 0xd000).contentEquals(before.copyOfRange(0x9000, 0xd000)), "nvs (Wi-Fi) untouched")
    check(flash.copyOfRange(0x810000, total).contentEquals(before.copyOfRange(0x810000, total)), "storage (data) untouched")
    check(flash.copyOfRange(0x410000, 0x810000).contentEquals(before.copyOfRange(0x410000, 0x810000)), "second app slot untouched")
    check(rom.resets >= 2, "watch was reset")

    // --- полная прошивка
    val flash2 = before.copyOf()
    val ef2 = EspFlasher(FakeRom(flash2))
    ef2.connect()
    ef2.write(FlashPlan.plan(img, false))
    check(flash2.copyOfRange(0, total).contentEquals(img), "full image replaces everything including storage")

    // --- ошибки
    val bad = before.copyOf()
    try {
        val e3 = EspFlasher(FakeRom(bad, corrupt = true)); e3.connect(); e3.write(plan)
        check(false, "corrupted write is detected")
    } catch (e: FlashException) { check(e.message!!.contains("check failed"), "corrupted write is detected by MD5") }
    check(FlashPlan.problem(ByteArray(100000)) != null, "app-only / small file is rejected")
    check(FlashPlan.problem(ByteArray(0x30000)) != null, "file without bootloader magic is rejected")
    val noTable = img.copyOf(); java.util.Arrays.fill(noTable, 0x8000, 0x8100, 0xFF.toByte())
    check(FlashPlan.problem(noTable) != null, "file without partition table is rejected")
    check(EspFlasher.slip(byteArrayOf(0xC0.toByte(), 0xDB.toByte(), 1)).contentEquals(byteArrayOf(0xC0.toByte(), 0xDB.toByte(), 0xDC.toByte(), 0xDB.toByte(), 0xDD.toByte(), 1, 0xC0.toByte())), "SLIP escapes C0 and DB")

    println(if (failures == 0) "RESULT: PASS" else "RESULT: FAIL ($failures)")
    exitProcess(if (failures == 0) 0 else 1)
}
