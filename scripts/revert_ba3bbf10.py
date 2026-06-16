#!/usr/bin/env python3
"""Apply ba3bbf10 StrReplace reversals only (June 11 morning session)."""
import json
from pathlib import Path

PROJECT = Path(r"c:\Users\erdem\ESP32\New-Project")
SESSION = "ba3bbf10-d6d2-4d12-9fec-ca5f17c6721d"
JSONL = Path(rf"C:\Users\erdem\.cursor\projects\c-Users-erdem-ESP32-New-Project\agent-transcripts\{SESSION}\{SESSION}.jsonl")


def norm_path(p: str) -> Path:
    p = p.replace("\\", "/")
    if "New-Project/" in p:
        p = p.split("New-Project/")[-1]
    return PROJECT / p


def main():
    reps = []
    for line in JSONL.read_text(encoding="utf-8", errors="replace").splitlines():
        obj = json.loads(line)
        for part in obj.get("message", {}).get("content", []):
            if part.get("type") == "tool_use" and part.get("name") == "StrReplace":
                inp = part.get("input", {})
                reps.append(inp)

    ok = fail = 0
    for inp in reversed(reps):
        fp = norm_path(inp["path"])
        old, new = inp["old_string"], inp["new_string"]
        text = fp.read_text(encoding="utf-8").replace("\r\n", "\n")
        new_n = new.replace("\r\n", "\n")
        old_n = old.replace("\r\n", "\n")
        if new_n not in text:
            print(f"SKIP {fp.name}: not found ({new_n[:60]!r}...)")
            fail += 1
            continue
        text = text.replace(new_n, old_n, 1)
        fp.write_text(text, encoding="utf-8", newline="\n")
        print(f"OK {fp.relative_to(PROJECT)}")
        ok += 1
    print(f"\n{ok} ok, {fail} skip")


if __name__ == "__main__":
    main()
