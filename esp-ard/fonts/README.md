# Turkish UI fonts

Montserrat with Latin Extended (ğ, ü, ş, ı, ö, ç, İ, °, …).

Regenerate after changing UI copy:

```bash
npx lv_font_conv --font fonts/Montserrat-Medium.ttf --size 10 --bpp 4 \
  -r "0x20-0x7F" -r "0xA0-0x17F" -r "0xB0" -r "0x2013-0x2014" -r "0x2022" \
  --symbols --format lvgl --no-compress --force-fast-kern-format \
  --lv-font-name lv_font_tr_10 -o src/fonts/lv_font_tr_10.c
```

Repeat for sizes 12 and 14 (`lv_font_tr_12`, `lv_font_tr_14`).
