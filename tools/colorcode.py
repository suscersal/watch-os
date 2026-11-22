#!/usr/bin/env python3
"""Рисует цветной код 16x16 (формат: src/watch/colorcode.h) для ручного ввода пароля Wi-Fi на часах.

  python3 tools/colorcode.py --ssid MyWiFi --password 'secret123'            # в терминале (цвета ANSI + буквы)
  python3 tools/colorcode.py --ssid MyWiFi --password 'secret123' --png code.png   # картинка для телефона (нужен Pillow)

Цвета: R красный, G зелёный, B синий, Y жёлтый. Буква в клетке нужна, чтобы не путать цвета.
Вводить нужно все 16x16 клеток, включая последний столбец и последнюю строку (это проверочные).
"""
import argparse
import sys

N, DATA_N, DATA_BYTES, MAX_DATA = 16, 15, 56, 52
LETTERS = "RGBY"
RGB = [(0xE1, 0x1D, 0x48), (0x22, 0xC5, 0x5E), (0x3B, 0x82, 0xF6), (0xFA, 0xCC, 0x15)]


def crc16(data: bytes) -> int:
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def encode(type_: int, data: bytes):
    if len(data) > MAX_DATA:
        raise ValueError(f"слишком длинно: {len(data)} байт, максимум {MAX_DATA}")
    buf = bytearray(DATA_BYTES)
    buf[0], buf[1] = type_, len(data)
    buf[2:2 + len(data)] = data
    c = crc16(bytes(buf[:2 + len(data)]))
    buf[2 + len(data)], buf[3 + len(data)] = c >> 8, c & 0xFF
    cells = [[0] * N for _ in range(N)]
    for k in range(DATA_N * DATA_N):
        bit = 2 * k
        v = (buf[bit // 8] >> (6 - bit % 8)) & 3 if bit // 8 < DATA_BYTES else 0
        cells[k // DATA_N][k % DATA_N] = v
    for r in range(DATA_N):
        x = 0
        for c in range(DATA_N):
            x ^= cells[r][c]
        cells[r][15] = x
    for c in range(N):
        x = 0
        for r in range(DATA_N):
            x ^= cells[r][c]
        cells[15][c] = x
    return cells


def wifi(ssid: str, password: str):
    s, p = ssid.encode(), password.encode()
    if not s or len(s) > 32 or len(p) > 63:
        raise ValueError("SSID 1..32 байт, пароль до 63 байт")
    return encode(1, s + b"\0" + p)


def show_terminal(cells):
    print("     " + " ".join(f"{c + 1:2d}" for c in range(N)))
    for r, row in enumerate(cells):
        out = []
        for v in row:
            R, G, B = RGB[v]
            fg = "30" if v in (1, 3) else "97"
            out.append(f"\x1b[48;2;{R};{G};{B}m\x1b[{fg}m {LETTERS[v]} \x1b[0m")
        print(f"{r + 1:3d}  " + "".join(out) + "   " + "".join(LETTERS[v] for v in row))


def save_png(cells, path, cell=44):
    from PIL import Image, ImageDraw
    pad = 30
    size = pad + cell * N
    img = Image.new("RGB", (size, size), (17, 24, 39))
    d = ImageDraw.Draw(img)
    for i in range(N):
        d.text((pad + i * cell + cell // 2 - 6, 8), str(i + 1), fill=(156, 163, 175))
        d.text((6, pad + i * cell + cell // 2 - 6), str(i + 1), fill=(156, 163, 175))
    for r in range(N):
        for c in range(N):
            v = cells[r][c]
            x, y = pad + c * cell, pad + r * cell
            d.rectangle([x, y, x + cell - 2, y + cell - 2], fill=RGB[v])
            d.text((x + cell // 2 - 5, y + cell // 2 - 7), LETTERS[v], fill=(0, 0, 0) if v in (1, 3) else (255, 255, 255))
    # проверочные строка и столбец отделены линией
    d.line([pad + 15 * cell - 1, pad, pad + 15 * cell - 1, size], fill=(255, 255, 255), width=2)
    d.line([pad, pad + 15 * cell - 1, size, pad + 15 * cell - 1], fill=(255, 255, 255), width=2)
    img.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ssid", required=True)
    ap.add_argument("--password", default="")
    ap.add_argument("--png", help="сохранить картинку")
    ap.add_argument("--hex", action="store_true", help="напечатать сетку строками из букв (для тестов)")
    a = ap.parse_args()
    try:
        cells = wifi(a.ssid, a.password)
    except ValueError as e:
        sys.exit(f"Ошибка: {e}")
    if a.hex:
        for row in cells:
            print("".join(LETTERS[v] for v in row))
        return
    show_terminal(cells)
    if a.png:
        save_png(cells, a.png)
        print("Сохранено:", a.png)


if __name__ == "__main__":
    main()
