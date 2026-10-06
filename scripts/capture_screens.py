#!/usr/bin/env python3
"""Capture README screenshots from a board running the screenshot-tour build.

The tour firmware (CONFIG_UI_SHOT_TOUR=y, see main/ui/ui_shots.c) walks every
screen and prints each LVGL snapshot over the USB console as

    SHOT:BEGIN <name> <w> <h> <lines>
    SHOT:D <index> <base64 of 57 bytes of RGB565 little-endian pixels>
    SHOT:END <name>
    ...
    SHOT:DONE

The USB console can drop lines under load, so every frame is sent twice with
numbered lines; missing or corrupted lines of one pass are filled from the
other. This script resets the board, collects the frames and writes
<out>/<name>.png (RGBA, outside of the round panel made transparent).
Needs only pyserial (bundled with the ESP-IDF Python env).

    python scripts/capture_screens.py --port COM3 --out docs/screenshots
"""

import argparse
import base64
import os
import struct
import sys
import time
import zlib

import serial

PANEL_R = 230.0  # visible circle radius of the 480x480 round LCD


def rgb565_to_rgba(raw, w, h):
    cx, cy = (w - 1) / 2.0, (h - 1) / 2.0
    rows = []
    for y in range(h):
        row = bytearray(b"\x00")  # PNG filter: none
        for x in range(w):
            v = raw[2 * (y * w + x)] | (raw[2 * (y * w + x) + 1] << 8)
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            a = max(0.0, min(1.0, PANEL_R + 0.5 - d))  # 1 px anti-aliased edge
            row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), int(a * 255)))
        rows.append(bytes(row))
    return b"".join(rows)


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rgba, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--out", default="docs/screenshots")
    ap.add_argument("--timeout", type=float, default=300.0, help="seconds to wait for SHOT:DONE")
    ap.add_argument("--no-reset", action="store_true", help="do not reset the board first")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    ser = serial.Serial(args.port, 115200, timeout=0.5)
    if hasattr(ser, "set_buffer_size"):
        ser.set_buffer_size(rx_size=1 << 20)
    if not args.no_reset:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.12)
        ser.rts = False

    def lines():
        # Bulk reads: pyserial's readline() reads byte by byte and is too slow
        # to keep the USB console drained (the ESP then drops output).
        pending = b""
        while True:
            data = ser.read(max(1, ser.in_waiting))
            if not data:
                yield None
                continue
            pending += data
            *full, pending = pending.split(b"\n")
            for raw in full:
                yield raw.decode("ascii", "replace").strip()

    deadline = time.time() + args.timeout
    frames = []                      # (name, w, h, lines, {index: bytes})
    cur = None
    done = False
    for line in lines():
        if done or time.time() > deadline:
            break
        if line is None:
            continue
        pos = line.find("SHOT:")
        if pos < 0:
            continue
        payload = line[pos + 5:]
        try:
            if payload.startswith("BEGIN "):
                _, name, w, h, n = payload.split()
                cur = (name, int(w), int(h), int(n), {})
                print(f"receiving {name} ({w}x{h})...", flush=True)
            elif payload.startswith("D ") and cur:
                _, i, data = payload.split(" ", 2)
                i = int(i)
                chunk = base64.b64decode(data, validate=True)
                total = cur[1] * cur[2] * 2
                want = min(57, total - i * 57)
                if 0 <= i < cur[3] and len(chunk) == want:
                    cur[4].setdefault(i, chunk)
            elif payload.startswith("END ") and cur:
                frames.append(cur)
                cur = None
            elif payload.startswith("FAIL "):
                print(f"  snapshot failed: {payload[5:]}", file=sys.stderr)
            elif payload == "DONE":
                done = True
        except (ValueError, base64.binascii.Error):
            pass   # corrupted line: the other pass has it
    if not done:
        print("timeout waiting for SHOT:DONE", file=sys.stderr)

    saved = 0
    for name, w, h, n, got in frames:
        missing = [i for i in range(n) if i not in got]
        if missing:
            print(f"  {name}: {len(missing)} line(s) missing, skipped", file=sys.stderr)
            continue
        raw = b"".join(got[i] for i in range(n))
        path = os.path.join(args.out, f"{name}.png")
        write_png(path, w, h, rgb565_to_rgba(raw, w, h))
        saved += 1
        print(f"  saved {path}", flush=True)
    print(f"{saved}/{len(frames)} screenshot(s) saved")
    return 0 if done and saved == len(frames) else 1


if __name__ == "__main__":
    sys.exit(main())
