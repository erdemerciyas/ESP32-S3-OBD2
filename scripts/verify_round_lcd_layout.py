#!/usr/bin/env python3
"""Verify OBD2 dashboard UI regions against Waveshare 480x480 round LCD geometry."""

from __future__ import annotations

import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
THEME_H = ROOT / "main" / "ui" / "theme.h"


@dataclass
class Rect:
    name: str
    x0: int
    y0: int
    x1: int
    y1: int
    screen: str = "all"

    @property
    def width(self) -> int:
        return self.x1 - self.x0

    @property
    def height(self) -> int:
        return self.y1 - self.y0

    def center(self) -> tuple[float, float]:
        return ((self.x0 + self.x1) / 2.0, (self.y0 + self.y1) / 2.0)

    def sample_points(self) -> list[tuple[str, float, float]]:
        cx, cy = self.center()
        return [
            ("TL", float(self.x0), float(self.y0)),
            ("TR", float(self.x1), float(self.y0)),
            ("BL", float(self.x0), float(self.y1)),
            ("BR", float(self.x1), float(self.y1)),
            ("C", cx, cy),
        ]


def parse_theme_constants(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    const: dict[str, int] = {}
    for name, expr in re.findall(r"#define\s+(UI_\w+)\s+(.+)", text):
        expr = expr.split("/*")[0].strip()
        if re.fullmatch(r"\d+", expr):
            const[name] = int(expr)
        elif "(" in expr:
            nums = [int(n) for n in re.findall(r"\d+", expr)]
            if "UI_PANEL_D" in expr and "UI_BEZEL" in expr and len(nums) == 2:
                const[name] = nums[0] - nums[1] * 2
    return const


def inside_circle(x: float, y: float, cx: float, cy: float, r: float) -> bool:
    dx, dy = x - cx, y - cy
    return dx * dx + dy * dy <= r * r


def chord_half_width(y: float, cy: float, r: float) -> float:
    dy = abs(y - cy)
    if dy >= r:
        return 0.0
    return math.sqrt(r * r - dy * dy)


def build_regions(c: dict[str, int]) -> list[Rect]:
    panel = c["UI_PANEL_D"]
    bezel = c["UI_BEZEL"]
    viewport = c["UI_VIEWPORT_SZ"]
    tab_x = bezel
    tab_y = bezel
    tab_w = viewport
    tab_h = viewport
    tab_b = tab_y + tab_h

    gauge = c["UI_GAUGE_SZ"]
    gauge_cx = tab_x + tab_w // 2
    gauge_cy = tab_y + tab_h // 2 - 20
    gauge_half = gauge // 2

    stat_lift = c["UI_STAT_LIFT"] + c["UI_DOT_H"]
    stat_h = c["UI_STAT_H"]
    stat_b = tab_b - stat_lift
    stat_t = stat_b - stat_h

    btn_h = c["UI_BTN_H"]
    btn_b = stat_b
    btn_t = btn_b - btn_h

    dot_h = c["UI_DOT_H"]
    dot_lift = c["UI_DOT_BAR_LIFT"]
    dot_b = panel - dot_lift
    dot_t = dot_b - dot_h

    pad_top = c["UI_PAD_TOP"]
    pad_hor = c["UI_PAD_HOR"]
    pad_bot = c["UI_PAD_BOT"]
    gap = c["UI_GAP"]
    header_h = 34  # font_lg 28 + row gap

    grid_top = tab_y + pad_top + header_h + gap
    grid_h = tab_h - pad_top - pad_bot - header_h - gap
    row_h = (grid_h - 2 * gap) / 3.0
    cell_w = (tab_w - 2 * pad_hor - 2 * gap) / 3.0

    regions: list[Rect] = [
        Rect("tabview", tab_x, tab_y, tab_x + tab_w, tab_b),
        Rect(
            "dash_gauge",
            gauge_cx - gauge_half,
            gauge_cy - gauge_half,
            gauge_cx + gauge_half,
            gauge_cy + gauge_half,
            "dash",
        ),
        Rect("dash_stats_row", tab_x, stat_t, tab_x + tab_w, stat_b, "dash"),
        Rect("connect_scan_btn", tab_x, btn_t, tab_x + tab_w, btn_b, "connect"),
        Rect("dot_bar", tab_x, dot_t, tab_x + tab_w, dot_b, "chrome"),
    ]

    for i, label in enumerate(("Speed", "Temp", "Volt")):
        x0 = int(tab_x + pad_hor + i * (cell_w + gap))
        x1 = int(x0 + cell_w)
        regions.append(Rect(f"dash_stat_{label.lower()}", x0, stat_t, x1, stat_b, "dash"))

    for r in range(3):
        y0 = int(grid_top + r * (row_h + gap))
        y1 = int(y0 + row_h)
        for col in range(3):
            x0 = int(tab_x + pad_hor + col * (cell_w + gap))
            x1 = int(x0 + cell_w)
            idx = r * 3 + col + 1
            regions.append(Rect(f"grid_cell_{idx}", x0, y0, x1, y1, "grid"))

    return regions


def analyze(regions: list[Rect], cx: float, cy: float, r: float) -> None:
    print(f"Panel {int(cx * 2)}x{int(cy * 2)}, visible circle R={r:.0f}px (diameter {2*r:.0f})")
    print(f"Center ({cx:.0f}, {cy:.0f})\n")

    issues: list[str] = []

    for rect in regions:
        outside = []
        for tag, x, y in rect.sample_points():
            if not inside_circle(x, y, cx, cy, r):
                outside.append(tag)

        cx_rect, cy_rect = rect.center()
        half = chord_half_width(cy_rect, cy, r)
        avail_w = 2 * half
        clipped_w = max(0.0, rect.width - avail_w)
        clipped_pct = (clipped_w / rect.width * 100.0) if rect.width else 0.0

        status = "OK"
        if outside:
            status = "CLIP"
            if any(t in outside for t in ("C",)) or len(outside) >= 3:
                issues.append(rect.name)
        elif clipped_pct > 15:
            status = "TIGHT"
            issues.append(rect.name)

        print(
            f"[{status:5}] {rect.name:22} "
            f"bbox=({rect.x0},{rect.y0})-({rect.x1},{rect.y1}) "
            f"outside={','.join(outside) if outside else '-'} "
            f"width_clip~={clipped_pct:.0f}%"
        )

    print()
    if issues:
        print("Elements with visible round-panel risk:")
        for name in issues:
            print(f"  - {name}")
    else:
        print("All checked regions fit inside the visible circle.")

    # Bottom band chord check (stats row midline)
    stat_y = next(r for r in regions if r.name == "dash_stats_row").center()[1]
    half = chord_half_width(stat_y, cy, r)
    print(
        f"\nAt stats mid-Y={stat_y:.0f}: visible chord width={2*half:.0f}px "
        f"vs stats row {next(r for r in regions if r.name == 'dash_stats_row').width}px"
    )


def main() -> int:
    if not THEME_H.exists():
        print(f"Missing {THEME_H}", file=sys.stderr)
        return 1

    c = parse_theme_constants(THEME_H)
    panel = c["UI_PANEL_D"]
    cx = cy = panel / 2.0
    r = c["UI_VIEWPORT_SZ"] / 2.0
    regions = build_regions(c)
    analyze(regions, cx, cy, r)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
