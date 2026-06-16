import json
from pathlib import Path
from datetime import datetime

transcripts = Path(r"C:\Users\erdem\.cursor\projects\c-Users-erdem-ESP32-New-Project\agent-transcripts")
exclude = {
    "ba3bbf10-d6d2-4d12-9fec-ca5f17c6721d",
    "3a5c5924-b942-4499-9583-c28e1382b8de",
    "2e3ce5d0-77c5-4bf1-8d35-5cdd9186de6c",
}
files_of_interest = {
    "main/obd/ble_obd.c", "main/obd/ble_obd.h", "main/obd/elm327.c", "main/obd/elm327.h",
    "main/obd/obd_pids.c", "main/obd/obd_dtc.c", "main/ui/screen_dtc.c", "main/ui/ui.c", "main/ui/ui.h",
    "main/ui/screen_dash.c", "main/ui/screen_settings.c", "main/data/vehicle_data.c", "main/data/vehicle_data.h",
    "main/data/vehicle_profile.c", "main/data/vehicle_profile.h",
}
out_dir = Path(r"c:\Users\erdem\ESP32\New-Project\_restore_jun10")
out_dir.mkdir(exist_ok=True)
found = {}

for jsonl in transcripts.rglob("*.jsonl"):
    if jsonl.parent.name in exclude or "subagents" in jsonl.parts:
        continue
    mtime = jsonl.stat().st_mtime
    for line in jsonl.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        for part in obj.get("message", {}).get("content", []):
            if part.get("type") != "tool_use":
                continue
            inp = part.get("input", {})
            if inp.get("name") != "Write":
                continue
            raw = inp.get("path", "").replace("\\", "/")
            if "New-Project/" not in raw:
                continue
            p = raw.split("New-Project/")[-1]
            if p not in files_of_interest:
                continue
            prev = found.get(p)
            if prev is None or mtime >= prev[0]:
                found[p] = (mtime, jsonl.parent.name, inp.get("contents", ""))

for p, (mtime, sess, contents) in sorted(found.items()):
    print(datetime.fromtimestamp(mtime), sess, len(contents), p)
    rel = Path(p)
    dest = out_dir / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(contents, encoding="utf-8", newline="\n")

print("WROTE", len(found), "files to", out_dir)
