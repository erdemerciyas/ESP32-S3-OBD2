# UI fonts

- `font_ui_*.c` — Montserrat subset with Turkish characters (`ui_charset.txt`).
- `font_icons_*.c` — Font Awesome glyphs for `LV_SYMBOL_*` icons.

Regenerate after changing charset or icon set:

```powershell
.\gen_fonts.ps1
```

Requires Node.js in PATH. Uses local `npm install lv_font_conv` (do not pass charset via PowerShell `@` — it strips letters). UI range: `0x20-0x7E,0xA0-0x17F`.
