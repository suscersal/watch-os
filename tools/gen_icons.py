#!/usr/bin/env python3
"""Генерирует assets/icons/<ключ>.svg: круглые иконки 112x112 (как ICON в launcher.c).
Запуск из корня репозитория: python3 tools/gen_icons.py"""
import os

def P(d): return f'<path d="{d}"/>'
def C(x, y, r): return f'<circle cx="{x}" cy="{y}" r="{r}"/>'
def R(x, y, w, h, rx): return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}"/>'

ICONS = [
 ("calc", "#FF9F0A", R(4,2,16,20,2)+P("M8 6h8")+P("M8 11h.01M12 11h.01M16 11h.01M8 15h.01M12 15h.01M16 15h.01M8 19h.01M12 19h.01M16 19h.01")),
 ("list", "#06B6D4", P("M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01")),
 ("edit", "#3B82F6", P("M12 20h9")+P("M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4Z")),
 ("bell", "#F43F5E", P("M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9")+P("M10.3 21a1.94 1.94 0 0 0 3.4 0")),
 ("gps", "#30D158", P("M20 10c0 6-8 12-8 12s-8-6-8-12a8 8 0 0 1 16 0Z")+C(12,10,3)),
 ("settings", "#8E8E93", P("M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6")),
 ("mail", "#0A84FF", R(2,4,20,16,2)+P("m22 7-8.97 5.7a1.94 1.94 0 0 1-2.06 0L2 7")),
 ("audio", "#FF375F", P("M9 18V5l12-2v13")+C(6,18,3)+C(18,16,3)),
 ("image", "#BF5AF2", R(3,3,18,18,2)+C(9,9,2)+P("m21 15-3.09-3.09a2 2 0 0 0-2.82 0L6 21")),
 ("call", "#30D158", P("M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6 19.79 19.79 0 0 1-3.07-8.67A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72 12.84 12.84 0 0 0 .7 2.81 2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45 12.84 12.84 0 0 0 2.81.7A2 2 0 0 1 22 16.92z")),
 ("plus", "#0A84FF", P("M12 5v14M5 12h14")),
 ("ok", "#30D158", P("M20 6 9 17l-5-5")),
 ("home", "#F59E0B", P("m3 9 9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z")+P("M9 22V12h6v10")),
 ("file", "#64748B", P("M14.5 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7.5z")+P("M14 2v6h6")),
 ("folder", "#8B5CF6", P("M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z")),
 ("video", "#FF453A", P("m22 8-6 4 6 4V8Z")+R(2,6,14,12,2)),
 ("wifi", "#EF4444", P("M5 12.55a11 11 0 0 1 14.08 0M1.42 9a16 16 0 0 1 21.16 0M8.53 16.11a6 6 0 0 1 6.95 0M12 20h.01")),
 ("battery", "#34C759", R(2,7,16,10,2)+P("M22 11v2M6 11v2M10 11v2")),
 ("charge", "#FFD60A", P("M13 2 3 14h9l-1 8 10-12h-9l1-8z")),
 ("refresh", "#5E5CE6", P("M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8M21 3v5h-5M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16M8 16H3v5")),
 ("play", "#FF9F0A", '<path fill="#fff" d="M6 3 20 12 6 21Z"/>'),
 ("trash", "#FF453A", P("M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M10 11v6M14 11v6")),
 ("download", "#0A84FF", P("M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3")),
 ("mic", "#FF375F", P("M12 2a3 3 0 0 0-3 3v7a3 3 0 0 0 6 0V5a3 3 0 0 0-3-3Z")+P("M19 10v2a7 7 0 0 1-14 0v-2M12 19v3")),
 ("timer", "#FF9F0A", P("M10 2h4M12 14l3-3")+C(12,14,8)),
]

def mix(h, t):
    n = int(h[1:], 16)
    return "#%02x%02x%02x" % tuple(round(v + (255 - v) * t) for v in ((n >> 16) & 255, (n >> 8) & 255, n & 255))

def svg(col, glyph):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="112" height="112" viewBox="0 0 112 112">'
            f'<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{mix(col, 0.22)}"/>'
            f'<stop offset="1" stop-color="{col}"/></linearGradient></defs>'
            f'<circle cx="56" cy="56" r="56" fill="url(#g)"/>'
            f'<g transform="translate(30.8 30.8) scale(2.1)" fill="none" stroke="#fff" stroke-width="2" '
            f'stroke-linecap="round" stroke-linejoin="round">{glyph}</g></svg>\n')

if __name__ == "__main__":
    os.makedirs("assets/icons", exist_ok=True)
    for key, col, glyph in ICONS:
        with open(f"assets/icons/{key}.svg", "w", encoding="utf-8") as f:
            f.write(svg(col, glyph))
    print(len(ICONS), "icons -> assets/icons/")
