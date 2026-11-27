package com.watchos.sync

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Build
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** USB CDC-ACM порт часов (ESP32-S3 USB-Serial-JTAG, 303A:1001) поверх android.hardware.usb, без внешних библиотек. */
class UsbCdcPort(
    private val conn: UsbDeviceConnection,
    private val ctlIf: UsbInterface,
    private val dataIf: UsbInterface,
    private val epIn: UsbEndpoint,
    private val epOut: UsbEndpoint
) : FlashPort {
    override fun write(data: ByteArray) {
        var off = 0
        while (off < data.size) {
            val n = minOf(4096, data.size - off)
            val chunk = data.copyOfRange(off, off + n)
            if (conn.bulkTransfer(epOut, chunk, n, 5000) < 0) throw FlashException(tr("USB write failed (cable unplugged?)", "ошибка записи по USB (кабель отключён?)"))
            off += n
        }
    }

    override fun read(buf: ByteArray, timeoutMs: Int): Int {
        val r = conn.bulkTransfer(epIn, buf, buf.size, timeoutMs)
        return if (r < 0) 0 else r
    }

    override fun setLines(dtr: Boolean, rts: Boolean) {
        val v = (if (dtr) 1 else 0) or (if (rts) 2 else 0)
        conn.controlTransfer(0x21, 0x22, v, ctlIf.id, null, 0, 1000)    // SET_CONTROL_LINE_STATE
    }

    fun close() {
        try { conn.releaseInterface(dataIf); conn.releaseInterface(ctlIf) } catch (_: Exception) {}
        conn.close()
    }
}

object UsbFlash {
    private const val ACTION = "com.watchos.sync.USB_PERMISSION"
    private const val VID = 0x303A
    private const val PID = 0x1001

    fun find(ctx: Context): UsbDevice? {
        val m = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
        return m.deviceList.values.firstOrNull { it.vendorId == VID && it.productId == PID }
    }

    /** Блокирует поток, пока человек отвечает на системный запрос доступа к USB (вызывать не из главного потока). */
    fun permission(ctx: Context, dev: UsbDevice): Boolean {
        val m = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
        if (m.hasPermission(dev)) return true
        val latch = CountDownLatch(1)
        val rx = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) { latch.countDown() }
        }
        val f = IntentFilter(ACTION)
        if (Build.VERSION.SDK_INT >= 33) ctx.registerReceiver(rx, f, Context.RECEIVER_NOT_EXPORTED) else ctx.registerReceiver(rx, f)
        try {
            val flags = if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
            val pi = PendingIntent.getBroadcast(ctx, 0, Intent(ACTION).setPackage(ctx.packageName), flags)
            m.requestPermission(dev, pi)
            latch.await(60, TimeUnit.SECONDS)
        } finally {
            ctx.unregisterReceiver(rx)
        }
        return m.hasPermission(dev)
    }

    fun open(ctx: Context, dev: UsbDevice): UsbCdcPort {
        val m = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
        val conn = m.openDevice(dev) ?: throw FlashException(tr("Cannot open the USB device", "не удаётся открыть USB-устройство"))
        var ctl: UsbInterface? = null
        var dat: UsbInterface? = null
        var ein: UsbEndpoint? = null
        var eout: UsbEndpoint? = null
        for (i in 0 until dev.interfaceCount) {
            val itf = dev.getInterface(i)
            if (itf.interfaceClass == UsbConstants.USB_CLASS_COMM && ctl == null) ctl = itf
            if (itf.interfaceClass == UsbConstants.USB_CLASS_CDC_DATA) {
                for (e in 0 until itf.endpointCount) {
                    val ep = itf.getEndpoint(e)
                    if (ep.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
                    if (ep.direction == UsbConstants.USB_DIR_IN) ein = ep else eout = ep
                }
                dat = itf
            }
        }
        val c = ctl; val d = dat; val ei = ein; val eo = eout
        if (c == null || d == null || ei == null || eo == null) {
            conn.close()
            throw FlashException(tr("The USB device has no serial interface", "у USB-устройства нет последовательного интерфейса"))
        }
        conn.claimInterface(c, true)
        conn.claimInterface(d, true)
        return UsbCdcPort(conn, c, d, ei, eo)
    }
}
