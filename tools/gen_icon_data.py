#!/usr/bin/env python3
"""assets/icons/*.svg -> src/watch/icon_data.c (LVGL, RGB565A8, 112x112, лежат во flash).
Нужны: pip install cairosvg pillow. Запуск из корня репозитория: python3 tools/gen_icon_data.py"""
import glob, io, os
import cairosvg
from PIL import Image

SIZE = 112
out = ['// icon_data.c - СГЕНЕРИРОВАНО tools/gen_icon_data.py из assets/icons/*.svg. Не править руками.',
       '#include "watch/icon_data.h"', '']
keys = []
for path in sorted(glob.glob("assets/icons/*.svg")):
    key = os.path.splitext(os.path.basename(path))[0]
    png = cairosvg.svg2png(url=path, output_width=SIZE, output_height=SIZE)
    im = Image.open(io.BytesIO(png)).convert("RGBA")
    rgb, alpha = bytearray(), bytearray()
    for r, g, b, a in im.getdata():
        if a == 0: r = g = b = 0
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        rgb += bytes((v & 255, v >> 8))      # RGB565, little endian
        alpha.append(a)
    data = bytes(rgb + alpha)                # плоскость RGB565, затем плоскость A8
    out.append(f'static const uint8_t ico_{key}_data[] = {{')
    for i in range(0, len(data), 24):
        out.append('    ' + ','.join(str(b) for b in data[i:i + 24]) + ',')
    out.append('};')
    out.append(f'static const lv_image_dsc_t ico_{key} = {{')
    out.append(f'    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .w = {SIZE}, .h = {SIZE}, .stride = {SIZE * 2}}},')
    out.append(f'    .data_size = sizeof(ico_{key}_data), .data = ico_{key}_data,')
    out.append('};\n')
    keys.append(key)
out.append('static const struct { const char *key; const lv_image_dsc_t *img; } icon_tbl[] = {')
out += [f'    {{"{k}", &ico_{k}}},' for k in keys]
out += ['};', '',
        'const lv_image_dsc_t *icon_image(const char *key)',
        '{', '    for (size_t i = 0; i < sizeof(icon_tbl) / sizeof(icon_tbl[0]); i++)',
        '        if (key && strcmp(icon_tbl[i].key, key) == 0) return icon_tbl[i].img;',
        '    return NULL;', '}', '']
open("src/watch/icon_data.c", "w").write("\n".join(out))
print(len(keys), "icons ->", "src/watch/icon_data.c")
