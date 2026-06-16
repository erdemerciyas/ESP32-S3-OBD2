#!/usr/bin/env python3
import json
from pathlib import Path

JSONL = Path(r"C:\Users\erdem\.cursor\projects\c-Users-erdem-ESP32-New-Project\agent-transcripts\ba3bbf10-d6d2-4d12-9fec-ca5f17c6721d\ba3bbf10-d6d2-4d12-9fec-ca5f17c6721d.jsonl")
OUT = Path(r"c:\Users\erdem\ESP32\New-Project\_restore_jun10\snapshots")

targets = {"elm327.c", "ble_obd.c", "screen_settings.c", "obd_pids.c"}

for line in JSONL.read_text(encoding="utf-8").splitlines():
    obj = json.loads(line)
    for part in obj.get("message", {}).get("content", []):
        if part.get("name") != "StrReplace":
            continue
        inp = part["input"]
        name = Path(inp["path"]).name
        if name not in targets:
            continue
        old = inp["old_string"]
        key = old[:40].replace("\n", "_").replace(" ", "_")
        (OUT / name).mkdir(parents=True, exist_ok=True)
        idx = len(list((OUT / name).glob("*.txt")))
        (OUT / name / f"{idx:03d}_old.txt").write_text(old, encoding="utf-8")
        print(f"{name} #{idx}: {old[:70]}...")
