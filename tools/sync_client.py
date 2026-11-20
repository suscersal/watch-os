#!/usr/bin/env python3
"""Клиент синхронизации часов (протокол v1, см. docs/sync-protocol.md).

Нужен тем, у кого нет Android-приложения: отправляет с компьютера время и фото.
Требуется Pillow (pip install pillow) для подготовки фото.

  tools/sync_client.py 192.168.0.5 8765 123456 --time --list photo1.jpg photo2.png
"""
import argparse, binascii, datetime, os, socket, struct, sys, time

T_HELLO, T_TIME, T_BEGIN, T_DATA, T_END, T_LIST, T_DELETE, T_BYE = 0x01, 0x02, 0x10, 0x11, 0x12, 0x13, 0x14, 0x7F
R_OK, R_HELLO_OK, R_ERR, R_LIST = 0x80, 0x81, 0x8F, 0x91
ERRORS = {1: "bad frame", 2: "wrong code", 3: "wrong state", 4: "too big", 5: "crc mismatch",
          6: "bad image format", 7: "storage full", 8: "unsupported", 9: "watch is locked"}
FULL_W, FULL_H, THUMB = 410, 502, 120
CHUNK = 16384


class WatchError(Exception):
    pass


class Watch:
    def __init__(self, host, port, timeout=10):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def _send(self, t, payload=b""):
        self.s.sendall(struct.pack("<BI", t, len(payload)) + payload)

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                raise WatchError("connection closed by the watch")
            buf += chunk
        return buf

    def _reply(self, req):
        t, n = struct.unpack("<BI", self._recv_exact(5))
        p = self._recv_exact(n)
        if t == R_ERR:
            raise WatchError(ERRORS.get(p[1], "error %d" % p[1]) + " (request 0x%02x)" % p[0])
        return t, p

    def hello(self, code, name="pc"):
        self._send(T_HELLO, bytes([1]) + code.encode("ascii") + name.encode("utf-8")[:32])
        t, p = self._reply(T_HELLO)
        proto, count, maxp, maxb = struct.unpack("<BHHI", p)
        return {"count": count, "max_photos": maxp, "max_full_bytes": maxb}

    def set_time(self, epoch=None, tz_min=None):
        now = time.time() if epoch is None else epoch
        if tz_min is None:
            tz_min = int(-time.timezone / 60 + (60 if time.localtime().tm_isdst > 0 else 0))
        self._send(T_TIME, struct.pack("<qh", int(now), tz_min))
        self._reply(T_TIME)

    def photo_list(self):
        self._send(T_LIST)
        t, p = self._reply(T_LIST)
        (n,) = struct.unpack_from("<H", p)
        return list(struct.unpack_from("<%dI" % n, p, 2))

    def delete(self, pid):
        self._send(T_DELETE, struct.pack("<I", pid))
        self._reply(T_DELETE)

    def send_photo(self, pid, taken, thumb, full, progress=None):
        crc = binascii.crc32(thumb + full) & 0xFFFFFFFF
        self._send(T_BEGIN, struct.pack("<IIIII", pid, taken, len(thumb), len(full), crc))
        t, p = self._reply(T_BEGIN)
        if p[1] == 1:
            return False                      # такое фото уже есть
        data, sent = thumb + full, 0
        while sent < len(data):
            part = data[sent:sent + CHUNK]
            self._send(T_DATA, part)
            sent += len(part)
            if progress:
                progress(sent, len(data))
        self._send(T_END)
        self._reply(T_END)
        return True

    def close(self):
        try:
            self._send(T_BYE)
        except OSError:
            pass
        self.s.close()


def prepare(path):
    """Файл любого формата -> (id, снято, JPEG миниатюры, JPEG полного кадра)."""
    from PIL import Image, ImageOps
    import io
    raw = open(path, "rb").read()
    pid = binascii.crc32(raw) & 0xFFFFFFFF
    im = Image.open(path)
    taken = int(os.path.getmtime(path))
    try:
        exif = im.getexif()
        s = exif.get_ifd(0x8769).get(0x9003) or exif.get(0x0132)
        if s:
            taken = int(datetime.datetime.strptime(s, "%Y:%m:%d %H:%M:%S").timestamp())
    except Exception:
        pass
    im = ImageOps.exif_transpose(im).convert("RGB")
    full = im.copy()
    full.thumbnail((FULL_W, FULL_H))
    th = ImageOps.fit(im, (THUMB, THUMB))
    fb, tb = io.BytesIO(), io.BytesIO()
    full.save(fb, "JPEG", quality=80, progressive=False)     # TJpgDec читает только baseline
    th.save(tb, "JPEG", quality=75, progressive=False)
    return pid, taken, tb.getvalue(), fb.getvalue()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host"); ap.add_argument("port", type=int); ap.add_argument("code")
    ap.add_argument("--time", action="store_true", help="синхронизировать время")
    ap.add_argument("--list", action="store_true", help="показать id фото на часах")
    ap.add_argument("--delete", type=lambda x: int(x, 16), action="append", default=[], help="удалить фото по id (hex)")
    ap.add_argument("photos", nargs="*")
    a = ap.parse_intermixed_args()
    w = Watch(a.host, a.port)
    try:
        info = w.hello(a.code)
        print("paired: %d/%d photos on the watch" % (info["count"], info["max_photos"]))
        if a.time:
            w.set_time(); print("time synced")
        for pid in a.delete:
            w.delete(pid); print("deleted %08x" % pid)
        for path in a.photos:
            pid, taken, tb, fb = prepare(path)
            ok = w.send_photo(pid, taken, tb, fb)
            print("%s %s (%d KB)" % ("sent" if ok else "skipped (already there)", path, (len(tb) + len(fb)) // 1024))
        if a.list:
            print("on the watch:", " ".join("%08x" % i for i in w.photo_list()))
    except WatchError as e:
        print("error:", e, file=sys.stderr); sys.exit(2)
    finally:
        w.close()


if __name__ == "__main__":
    main()
