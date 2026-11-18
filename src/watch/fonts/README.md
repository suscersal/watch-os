Шрифты сгенерированы так (Node.js, `npm i lv_font_conv`, файлы шрифтов лежат в lvgl/scripts/built_in_font/):

    R="0x20-0x7E,0xB0,0xAB,0xBB,0x401,0x410-0x44F,0x451,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,0x2116"
    SYMS=<список кодов символов LV_SYMBOL_* из lvgl/scripts/built_in_font/built_in_font_gen.py>
    for S in 14 20 28; do
      lv_font_conv --bpp 4 --size $S --no-compress \
        --font Montserrat-Medium.ttf -r "$R" \
        --font FontAwesome5-Solid+Brands+Regular.woff -r "$SYMS" \
        --format lvgl --lv-font-name watch_font_$S --lv-include "lvgl/lvgl.h" \
        --force-fast-kern-format -o watch_font_$S.c
    done

В набор входят латиница, русский алфавит (включая Ё/ё), типографские знаки и все символы LV_SYMBOL_*.
Лицензия Montserrat: SIL OFL (см. lvgl/scripts/built_in_font/font_license).
