Шрифты: Inter (SIL OFL) из npm-пакета @fontsource/inter, символы LV_SYMBOL_* и несколько дополнительных - FontAwesome из lvgl/scripts/built_in_font/.
Генерация (Node.js, `npm i lv_font_conv @fontsource/inter`):

    F=node_modules/@fontsource/inter/files
    FA=lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff
    RL="0x20-0x7E,0xA0,0xB0,0xB7,0xAB,0xBB,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,0x2212"
    RC="0x401,0x410-0x44F,0x451,0x2116"
    SYMS=<коды LV_SYMBOL_* и 0xF130,0xF2EA,0xF2F9,0xF111,0xF0C8,0xF04D,0xF00B в десятичном виде через запятую>
    gen() { # имя размер насыщенность
      lv_font_conv --bpp 4 --size $2 --no-compress \
        --font $F/inter-latin-$3-normal.woff -r "$RL" \
        --font $F/inter-cyrillic-$3-normal.woff -r "$RC" \
        --font $FA -r "$SYMS" \
        --format lvgl --lv-font-name $1 --lv-include "lvgl/lvgl.h" --force-fast-kern-format -o $1.c
    }
    gen watch_font_14 14 400;  gen watch_font_20 20 400;  gen watch_font_28 28 500
    gen watch_font_b20 20 600; gen watch_font_b28 28 700
    # крупные цифры, только нужные знаки
    lv_font_conv --bpp 4 --size 64 --no-compress --font $F/inter-latin-700-normal.woff \
      -r "0x20,0x25,0x2B,0x2D,0x2E,0x30-0x3A,0x2212" --format lvgl --lv-font-name watch_font_d64 \
      --lv-include "lvgl/lvgl.h" --force-fast-kern-format -o watch_font_d64.c
    # цифры 40 px (диктофон по макету Figma)
    lv_font_conv --bpp 4 --size 40 --no-compress --font $F/inter-latin-500-normal.woff \
      -r "0x20,0x2E,0x30-0x3A" --format lvgl --lv-font-name watch_font_d40 \
      --lv-include "lvgl/lvgl.h" --force-fast-kern-format -o watch_font_d40.c
