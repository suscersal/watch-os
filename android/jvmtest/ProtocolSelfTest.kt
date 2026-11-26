// Проверка WatchClient на обычной JVM против настоящего ядра часов (tests/sync_server.c).
// Запускается скриптом tests/run_kotlin_selftest.py.
import com.watchos.sync.WatchClient
import com.watchos.sync.WatchException
import com.watchos.sync.ensureJfif
import com.watchos.sync.firmwareProblem
import java.awt.Color
import java.awt.image.BufferedImage
import java.io.ByteArrayOutputStream
import javax.imageio.ImageIO
import kotlin.system.exitProcess

var failures = 0
fun check(ok: Boolean, msg: String) { println((if (ok) "ok:   " else "FAIL: ") + msg); if (!ok) failures++ }

fun jpeg(w: Int, h: Int, c: Color): ByteArray {
    val img = BufferedImage(w, h, BufferedImage.TYPE_INT_RGB)
    val g = img.createGraphics(); g.color = c; g.fillRect(0, 0, w, h); g.dispose()
    val out = ByteArrayOutputStream(); ImageIO.write(img, "jpg", out); return out.toByteArray()
}

fun main(args: Array<String>) {
    val host = args[0]; val port = args[1].toInt(); val code = args[2]

    // --- ensureJfif
    val withJfif = jpeg(50, 50, Color.RED)
    check(withJfif[2] == 0xFF.toByte() && withJfif[3] == 0xE0.toByte(), "ImageIO output has JFIF")
    check(ensureJfif(withJfif).contentEquals(withJfif), "ensureJfif leaves a JFIF file untouched")
    val stripped = byteArrayOf(withJfif[0], withJfif[1]) + withJfif.copyOfRange(2 + 2 + (((withJfif[4].toInt() and 0xFF) shl 8) or (withJfif[5].toInt() and 0xFF)), withJfif.size)
    check(stripped[2] != 0xE0.toByte(), "test image really has no APP0")
    val fixed = ensureJfif(stripped)
    check(fixed.size == stripped.size + 18 && fixed[2] == 0xFF.toByte() && fixed[3] == 0xE0.toByte() && fixed[6] == 'J'.code.toByte(), "ensureJfif inserts the JFIF segment")

    // --- сеанс 1: неверный код
    try {
        WatchClient(host, port).use { it.connect(); it.hello(if (code == "000000") "111111" else "000000") }
        check(false, "wrong code must fail")
    } catch (e: WatchException) {
        check(e.message == "wrong code", "wrong code -> WatchException('wrong code')")
    }

    // --- сеанс 2: всё остальное
    WatchClient(host, port).use { w ->
        w.connect()
        val info = w.hello(code, "kotlin-test")
        check(info.maxPhotos == 100 && info.photoCount == 0, "HELLO_OK: 0 photos, limit 100")
        w.setTime(1_700_000_000L, 180)
        check(w.listPhotos().isEmpty(), "list is empty")

        val thumbA = jpeg(120, 120, Color.RED); val fullA = jpeg(410, 502, Color.BLUE)
        check(w.sendPhoto(0xA1B2C3D4L, 1_700_000_000L, thumbA, fullA), "photo A sent (id above 2^31 to check unsigned handling)")
        // файл без JFIF после ensureJfif тоже принимается
        val thumbB = ensureJfif(byteArrayOf(thumbA[0], thumbA[1]) + thumbA.copyOfRange(2 + 2 + (((thumbA[4].toInt() and 0xFF) shl 8) or (thumbA[5].toInt() and 0xFF)), thumbA.size))
        val fullB = ensureJfif(byteArrayOf(fullA[0], fullA[1]) + fullA.copyOfRange(2 + 2 + (((fullA[4].toInt() and 0xFF) shl 8) or (fullA[5].toInt() and 0xFF)), fullA.size))
        var progressCalls = 0
        check(w.sendPhoto(7L, 1_700_000_500L, thumbB, fullB) { _, _ -> progressCalls++ }, "photo B (re-fixed JFIF) sent")
        check(progressCalls >= 1, "progress callback called")
        check(!w.sendPhoto(7L, 1_700_000_500L, thumbB, fullB), "same photo again: skipped (returns false)")
        check(w.listPhotos() == setOf(0xA1B2C3D4L, 7L), "list returns both ids")
        try { w.sendPhoto(9L, 1L, thumbA, jpeg(600, 700, Color.GREEN)); check(false, "oversized image must fail") }
        catch (e: WatchException) { check(e.message!!.contains("format"), "oversized image -> format error") }

        // --- прошивка: проверка файла на телефоне и отправка в (подставной) OTA-слот
        fun image(n: Int): ByteArray = ByteArray(n) { (it * 31 + 7).toByte() }.also {
            it[0] = 0xE9.toByte(); it[12] = 9; it[13] = 0; it[32] = 0x32; it[33] = 0x54; it[34] = 0xCD.toByte(); it[35] = 0xAB.toByte()
        }
        val fw = image(150_000)
        check(firmwareProblem(fw.copyOf(512), fw.size.toLong()) == null, "firmwareProblem: watch_os.bin-like file accepted")
        check(firmwareProblem(fw.copyOf(512), 14L * 1024 * 1024) != null, "firmwareProblem: 14 MB full image rejected (too big)")
        check(firmwareProblem(fw.copyOf(512), 1000) != null, "firmwareProblem: tiny file rejected")
        check(firmwareProblem(ByteArray(512), fw.size.toLong()) != null, "firmwareProblem: not an ESP image rejected")
        check(firmwareProblem(fw.copyOf(512).also { it[13] = 1 }, fw.size.toLong()) != null, "firmwareProblem: other chip rejected")
        check(firmwareProblem(fw.copyOf(512).also { it[32] = 0 }, fw.size.toLong()) != null, "firmwareProblem: no app descriptor rejected")
        val fwCrc = java.util.zip.CRC32().apply { update(fw) }.value
        var fwProgress = 0L
        w.sendFirmware(fw.size.toLong(), fwCrc, { fw.inputStream() }) { d, _ -> fwProgress = d }
        check(fwProgress == fw.size.toLong(), "firmware sent, progress reached the end")
        // не образ: часы отвергают первый же блок, закрывают соединение, а клиент показывает причину (последняя проверка сеанса)
        val junk = ByteArray(150_000)
        try { w.sendFirmware(junk.size.toLong(), java.util.zip.CRC32().apply { update(junk) }.value, { junk.inputStream() }); check(false, "junk firmware must fail") }
        catch (e: Exception) { check(e is WatchException && e.message!!.contains("format"), "junk firmware -> watch error text (format), got: ${e.message}") }
    }
    println(if (failures == 0) "\nKOTLIN ALL PASSED" else "\n$failures FAILED")
    exitProcess(if (failures == 0) 0 else 1)
}
