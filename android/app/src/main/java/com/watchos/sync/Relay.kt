package com.watchos.sync

import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket

/**
 * Прозрачная передача TCP для моста ИИ (docs/ai-bridge.md): часы -> Watch Sync (порт 8787, вне VPN) -> Watch AI (127.0.0.1:8788, под VPN).
 * HTTP не разбирается: байты идут как есть в обе стороны, поэтому /ping, /ask и фото работают без изменений.
 */
object Relay {
    const val PORT = 8787            // сюда подключаются часы
    const val UPSTREAM_PORT = 8788   // Watch AI слушает только loopback

    private const val BAD_GATEWAY = "Watch AI is not running (open the Watch AI app and press Save and start)"

    /** Обслуживает одно соединение часов и закрывает его. [second] запускает вторую половину передачи в отдельном потоке. */
    fun serve(client: Socket, upstreamPort: Int = UPSTREAM_PORT, second: (Runnable) -> Unit) {
        var up: Socket? = null
        try {
            client.soTimeout = 90_000
            up = try {
                Socket().apply { connect(InetSocketAddress("127.0.0.1", upstreamPort), 2000); soTimeout = 90_000 }
            } catch (_: Exception) {
                fail(client.getOutputStream())
                return
            }
            val u = up
            second(Runnable { try { pipe(client.getInputStream(), u.getOutputStream()) } catch (_: Exception) {} })
            pipe(u.getInputStream(), client.getOutputStream())     // кончается, когда Watch AI закрыл соединение (Connection: close)
        } catch (_: Exception) {
        } finally {
            try { up?.close() } catch (_: Exception) {}
            try { client.close() } catch (_: Exception) {}
        }
    }

    private fun pipe(i: InputStream, o: OutputStream) {
        val buf = ByteArray(8192)
        while (true) {
            val n = i.read(buf)
            if (n < 0) break
            o.write(buf, 0, n)
            o.flush()
        }
    }

    private fun fail(o: OutputStream) {
        val b = BAD_GATEWAY.toByteArray()
        o.write("HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: ${b.size}\r\nConnection: close\r\n\r\n".toByteArray())
        o.write(b)
        o.flush()
    }
}
