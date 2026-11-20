#!/usr/bin/env python3
"""Создаёт тестовые картинки для tests/gallery_sync_test.c (Pillow)."""
import sys, os
from PIL import Image, ImageDraw
d = sys.argv[1]
os.makedirs(d, exist_ok=True)

def save(im, name, **kw):
    im.save(os.path.join(d, name), **kw)

# полный кадр A: верх синий, низ зелёный; миниатюра A красная
a = Image.new("RGB", (410, 502), (0, 200, 0)); ImageDraw.Draw(a).rectangle([0, 0, 410, 250], fill=(0, 0, 255))
save(a, "full_a.jpg", quality=85, progressive=False)
save(Image.new("RGB", (120, 120), (230, 0, 0)), "thumb_a.jpg", quality=85, progressive=False)
# B: жёлтый квадрат 300x300; миниатюра пурпурная
save(Image.new("RGB", (300, 300), (255, 255, 0)), "full_b.jpg", quality=85, progressive=False)
save(Image.new("RGB", (120, 120), (255, 0, 255)), "thumb_b.jpg", quality=85, progressive=False)
# без JFIF-заголовка (убираем сегмент APP0): декодер LVGL такой файл не узнает
raw = open(os.path.join(d, "full_b.jpg"), "rb").read()
assert raw[2:4] == b"\xff\xe0"
open(os.path.join(d, "nojfif.jpg"), "wb").write(raw[:2] + raw[4 + int.from_bytes(raw[4:6], "big") - 2 + 2:] if False else raw[:2] + raw[2 + 2 + int.from_bytes(raw[4:6], "big"):])
# неподходящие
save(Image.new("RGB", (100, 100), (9, 9, 9)), "prog.jpg", quality=85, progressive=True)
save(Image.new("RGB", (500, 600), (9, 9, 9)), "big.jpg", quality=85, progressive=False)
# исходники для проверки подготовки в sync_client.py (разные размеры и ориентации)
save(Image.new("RGB", (800, 1000), (0, 255, 255)), "src_c.png")
save(Image.new("RGB", (1000, 600), (255, 128, 0)), "src_d.png")
print("fixtures in", d)
