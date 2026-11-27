package com.watchos.sync

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.OpenableColumns
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

/**
 * Подготовка трека для часов: теги (исполнитель, название) и обложка из самого файла, звук в WAV PCM 16 бит моно 22050 Гц
 * (часы играют только WAV; моно 22050 - около 10 МБ на 4 минуты, лимит часов 24 МБ). Работает с mp3, m4a, flac, ogg, wav.
 * Вызывать ДО подключения к часам: длинный трек раскодируется дольше 20 секунд, а молчащее соединение часы закрывают.
 */
object AudioPrep {
    const val RATE = 22050
    const val COVER_SIZE = 144
    const val COVER_MAX = 20000

    class Result(val file: File, val name: String, val crc: Long, val cover: ByteArray?)

    fun prepare(ctx: Context, uri: Uri, onProgress: (Int) -> Unit = {}): Result {
        var artist = ""
        var title = ""
        var cover: ByteArray? = null
        val mmr = MediaMetadataRetriever()
        try {
            mmr.setDataSource(ctx, uri)
            artist = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST) ?: ""
            title = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE) ?: ""
            cover = mmr.embeddedPicture?.let { makeCover(it) }
        } catch (_: Exception) {
        } finally {
            try { mmr.release() } catch (_: Exception) { }
        }
        val base = when {
            title.isNotBlank() -> if (artist.isNotBlank()) "$artist - $title" else title
            else -> displayName(ctx, uri).substringBeforeLast('.')
        }
        val name = base.map { if (it in "/\\:*?\"<>|" || it < ' ') '_' else it }.joinToString("").trim().ifEmpty { "track" } + ".wav"

        val out = File(ctx.cacheDir, "track_${System.nanoTime()}.wav")
        try {
            if (!passThroughWav(ctx, uri, out)) decodeToWav(ctx, uri, out, onProgress)
            val crc = CRC32()
            out.inputStream().use { ins ->
                val buf = ByteArray(65536)
                while (true) { val r = ins.read(buf); if (r <= 0) break; crc.update(buf, 0, r) }
            }
            return Result(out, name, crc.value, cover)
        } catch (e: Exception) {
            out.delete()
            throw e
        }
    }

    private fun displayName(ctx: Context, uri: Uri): String {
        try {
            ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) return c.getString(0) ?: "track"
            }
        } catch (_: Exception) { }
        return uri.lastPathSegment ?: "track"
    }

    /** Квадрат 144x144, baseline JPEG с JFIF не больше 20000 байт; null, если не вышло. */
    fun makeCover(raw: ByteArray): ByteArray? {
        val bmp = BitmapFactory.decodeByteArray(raw, 0, raw.size) ?: return null
        val s = minOf(bmp.width, bmp.height)
        val sq = Bitmap.createBitmap(bmp, (bmp.width - s) / 2, (bmp.height - s) / 2, s, s)
        val small = Bitmap.createScaledBitmap(sq, COVER_SIZE, COVER_SIZE, true)
        for (q in intArrayOf(88, 80, 70, 60, 50, 40)) {
            val o = ByteArrayOutputStream()
            small.compress(Bitmap.CompressFormat.JPEG, q, o)
            val jpg = ensureJfif(o.toByteArray())
            if (jpg.size <= COVER_MAX) return jpg
        }
        return null
    }

    /** Уже подходящий WAV (PCM 16 бит, не больше 24 МБ) отдаём как есть. */
    private fun passThroughWav(ctx: Context, uri: Uri, out: File): Boolean {
        val head = ByteArray(44)
        val ok = ctx.contentResolver.openInputStream(uri)?.use { ins ->
            var n = 0
            while (n < 44) { val r = ins.read(head, n, 44 - n); if (r <= 0) break; n += r }
            if (n < 44) return@use false
            val b = ByteBuffer.wrap(head).order(ByteOrder.LITTLE_ENDIAN)
            String(head, 0, 4) == "RIFF" && String(head, 8, 4) == "WAVE" && String(head, 12, 4) == "fmt " &&
                b.getShort(20).toInt() == 1 && b.getShort(34).toInt() == 16
        } ?: false
        if (!ok) return false
        ctx.contentResolver.openInputStream(uri)?.use { ins ->
            out.outputStream().use { o ->
                val buf = ByteArray(65536)
                var total = 0L
                while (true) {
                    val r = ins.read(buf); if (r <= 0) break
                    total += r
                    if (total > Proto.AUDIO_MAX_BYTES) throw WatchException(tr("track is too long for the watch (max 24 MB)", "трек слишком длинный для часов (максимум 24 МБ)"))
                    o.write(buf, 0, r)
                }
            }
        }
        return true
    }

    private fun wavHeader(dataBytes: Long): ByteArray {
        val b = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
        b.put("RIFF".toByteArray()).putInt((36 + dataBytes).toInt()).put("WAVE".toByteArray())
        b.put("fmt ".toByteArray()).putInt(16).putShort(1).putShort(1).putInt(RATE).putInt(RATE * 2).putShort(2).putShort(16)
        b.put("data".toByteArray()).putInt(dataBytes.toInt())
        return b.array()
    }

    /** MediaExtractor + MediaCodec -> PCM 16 бит -> моно -> 22050 Гц (линейная интерполяция) -> WAV. */
    private fun decodeToWav(ctx: Context, uri: Uri, out: File, onProgress: (Int) -> Unit) {
        val ex = MediaExtractor()
        ex.setDataSource(ctx, uri, null)
        var fmt: MediaFormat? = null
        for (i in 0 until ex.trackCount) {
            val f = ex.getTrackFormat(i)
            if (f.getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true) { ex.selectTrack(i); fmt = f; break }
        }
        if (fmt == null) { ex.release(); throw WatchException(tr("no audio in this file", "в файле нет звука")) }
        val durationUs = if (fmt.containsKey(MediaFormat.KEY_DURATION)) fmt.getLong(MediaFormat.KEY_DURATION) else 0L
        val codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME)!!)
        codec.configure(fmt, null, null, 0)
        codec.start()
        var srcRate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        var channels = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        var step = srcRate.toDouble() / RATE
        var phase = 0.0
        var last = 0
        var written = 0L
        val info = MediaCodec.BufferInfo()
        var inDone = false
        var outDone = false
        try {
            RandomAccessFile(out, "rw").use { raf ->
                raf.write(wavHeader(0))
                val pending = ByteArrayOutputStream(1 shl 16)
                fun flush() {
                    if (pending.size() == 0) return
                    raf.write(pending.toByteArray()); written += pending.size(); pending.reset()
                    if (written > Proto.AUDIO_MAX_BYTES) throw WatchException(tr("track is too long for the watch (max 24 MB)", "трек слишком длинный для часов (максимум 24 МБ)"))
                }
                while (!outDone) {
                    if (!inDone) {
                        val ii = codec.dequeueInputBuffer(10000)
                        if (ii >= 0) {
                            val n = ex.readSampleData(codec.getInputBuffer(ii)!!, 0)
                            if (n < 0) { codec.queueInputBuffer(ii, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inDone = true }
                            else { codec.queueInputBuffer(ii, 0, n, ex.sampleTime, 0); ex.advance() }
                        }
                    }
                    val oi = codec.dequeueOutputBuffer(info, 10000)
                    if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        val nf = codec.outputFormat
                        srcRate = nf.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        channels = nf.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        step = srcRate.toDouble() / RATE
                    } else if (oi >= 0) {
                        val buf = codec.getOutputBuffer(oi)!!
                        buf.position(info.offset).limit(info.offset + info.size)
                        val sh = buf.order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
                        val frames = sh.remaining() / channels
                        for (f in 0 until frames) {
                            var sum = 0
                            for (c in 0 until channels) sum += sh.get()
                            val s = sum / channels
                            while (phase < 1.0) {
                                val v = (last + (s - last) * phase).toInt()
                                pending.write(v and 0xFF); pending.write((v shr 8) and 0xFF)
                                phase += step
                            }
                            phase -= 1.0
                            last = s
                        }
                        flush()
                        if (durationUs > 0) onProgress((info.presentationTimeUs * 100 / durationUs).toInt().coerceIn(0, 100))
                        codec.releaseOutputBuffer(oi, false)
                        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outDone = true
                    }
                }
                flush()
                raf.seek(0)
                raf.write(wavHeader(written))
            }
        } finally {
            try { codec.stop() } catch (_: Exception) { }
            codec.release()
            ex.release()
        }
        if (written < 2) throw WatchException(tr("could not decode this file", "не удалось декодировать файл"))
    }
}
