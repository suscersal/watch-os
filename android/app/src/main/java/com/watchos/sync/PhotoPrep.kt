package com.watchos.sync

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.net.Uri
import java.io.ByteArrayOutputStream
import java.text.SimpleDateFormat
import java.util.Locale
import java.util.zip.CRC32
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Фото, готовое к отправке на часы: полный кадр до 410x502 и квадратная миниатюра 120x120, оба baseline JPEG с JFIF. */
class Prepared(val id: Long, val takenSeconds: Long, val thumb: ByteArray, val full: ByteArray)

object PhotoPrep {
    const val FULL_W = 410
    const val FULL_H = 502
    const val THUMB = 120

    /** Устойчивый идентификатор фото: CRC32 исходного файла. По нему часы узнают, что фото уже есть. */
    fun idOf(ctx: Context, uri: Uri): Long {
        val crc = CRC32()
        ctx.contentResolver.openInputStream(uri)!!.use { s ->
            val buf = ByteArray(64 * 1024)
            while (true) {
                val n = s.read(buf)
                if (n < 0) break
                crc.update(buf, 0, n)
            }
        }
        return crc.value
    }

    fun prepare(ctx: Context, uri: Uri, id: Long): Prepared {
        val cr = ctx.contentResolver

        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        cr.openInputStream(uri)!!.use { BitmapFactory.decodeStream(it, null, bounds) }
        val w = bounds.outWidth
        val h = bounds.outHeight
        require(w > 0 && h > 0) { "cannot read image size" }

        // Декодируем сразу в уменьшенном виде (степень двойки), чтобы не держать в памяти 12-мегапиксельный кадр.
        val scale = min(min(FULL_W.toFloat() / w, FULL_H.toFloat() / h), 1f)
        val targetW = w * scale
        val targetH = h * scale
        var sample = 1
        while (w / (sample * 2) >= targetW && h / (sample * 2) >= targetH) sample *= 2
        val opts = BitmapFactory.Options().apply {
            inSampleSize = sample
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        var bmp = cr.openInputStream(uri)!!.use { BitmapFactory.decodeStream(it, null, opts) }
            ?: throw IllegalArgumentException(tr("cannot decode image", "не удалось открыть изображение"))

        // Ориентация из EXIF и время съёмки
        var taken = System.currentTimeMillis() / 1000
        cr.openInputStream(uri)!!.use { s ->
            val exif = ExifInterface(s)
            val m = Matrix()
            when (exif.getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)) {
                ExifInterface.ORIENTATION_ROTATE_90 -> m.postRotate(90f)
                ExifInterface.ORIENTATION_ROTATE_180 -> m.postRotate(180f)
                ExifInterface.ORIENTATION_ROTATE_270 -> m.postRotate(270f)
                ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> m.postScale(-1f, 1f)
                ExifInterface.ORIENTATION_FLIP_VERTICAL -> m.postScale(1f, -1f)
                ExifInterface.ORIENTATION_TRANSPOSE -> { m.postRotate(90f); m.postScale(-1f, 1f) }
                ExifInterface.ORIENTATION_TRANSVERSE -> { m.postRotate(-90f); m.postScale(-1f, 1f) }
            }
            if (!m.isIdentity) bmp = Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, m, true)
            exif.getAttribute(ExifInterface.TAG_DATETIME_ORIGINAL)?.let {
                try {
                    SimpleDateFormat("yyyy:MM:dd HH:mm:ss", Locale.US).parse(it)?.let { d -> taken = d.time / 1000 }
                } catch (_: Exception) { }
            }
        }

        // Миниатюра: квадрат по центру
        val side = min(bmp.width, bmp.height)
        val crop = Bitmap.createBitmap(bmp, (bmp.width - side) / 2, (bmp.height - side) / 2, side, side)
        val thumbBmp = Bitmap.createScaledBitmap(crop, THUMB, THUMB, true)

        // Полный кадр: вписать в 410x502, не увеличивая
        val s = min(min(FULL_W.toFloat() / bmp.width, FULL_H.toFloat() / bmp.height), 1f)
        val fullBmp = if (s < 1f) Bitmap.createScaledBitmap(bmp, max(1, (bmp.width * s).roundToInt()), max(1, (bmp.height * s).roundToInt()), true) else bmp

        return Prepared(id, taken, jpeg(thumbBmp, 75), jpeg(fullBmp, 80))
    }

    private fun jpeg(b: Bitmap, quality: Int): ByteArray {
        val out = ByteArrayOutputStream()
        b.compress(Bitmap.CompressFormat.JPEG, quality, out)   // baseline JPEG; прогрессивные Android не создаёт
        return ensureJfif(out.toByteArray())
    }
}
