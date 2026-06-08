# Gauge components (LVGL 8 / 480×480)

## Semicircular meter (RPM)

**Aliases:** gauge, dial, tachometer, arc meter, speedometer layout.

**Description:** Primary OBD RPM readout; value dominates center, scale shows thousands.

### Best practices

- Sweep ~270° with rotation 135° (matches automotive “horizon” arc).
- Scale `min=0`, `max=8` with labels implying ×1000 RPM.
- Three static color zones on track: safe (0–4.5), warn (4.5–6.5), redline (6.5–8).
- Dark track arc behind zones (`0x21262d`).
- Needle: high-contrast (`0xffffff`), width 4–6 px.
- Center numeric: Montserrat 48, unit label 16px muted (`0x8b949e`).
- Major ticks every 2 units; minor ticks subtle (`0x484f58`).
- Clamp updates to max RPM; no animation required on embedded (optional short anim OK).

### Common layouts

```
        [status bar]
    ┌─────────────────────┐
    │    ╭─────────╮      │
    │   ╱  4  6  8  ╲     │  ← meter + zones
    │  │    4523     │    │  ← value + RPM
    │   ╲  0  2  4  ╱     │
    │ [speed]     [volt]  │
    │    [temps][thr bar] │
    └─────────────────────┘
```

## Linear bar (throttle %)

**Aliases:** progress bar, level indicator.

### Best practices

- Range 0–100, height 8–12 px, radius 4 px.
- Show numeric `%` beside or below bar.
- Indicator color distinct from RPM (`0xd2a8ff` accent).

### Anti-patterns

- Knob on RPM meter (use needle only).
- Light background cards on sunlit car installs.
