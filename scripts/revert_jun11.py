#!/usr/bin/env python3
"""Reverse all StrReplace edits from June 11 sessions to restore June 10 (becc5257) state."""
import json
import re
from pathlib import Path

PROJECT = Path(r"c:\Users\erdem\ESP32\New-Project")
TRANSCRIPTS = Path(r"C:\Users\erdem\.cursor\projects\c-Users-erdem-ESP32-New-Project\agent-transcripts")

# Sessions from June 11 to revert (in chronological order for reversal we go reverse)
JUN11_SESSIONS = [
    "ba3bbf10-d6d2-4d12-9fec-ca5f17c6721d",
    "3a5c5924-b942-4499-9583-c28e1382b8de",
    "2e3ce5d0-77c5-4bf1-8d35-5cdd9186de6c",
]


def norm_path(p: str) -> Path:
    p = p.replace("\\", "/")
    if "New-Project/" in p:
        p = p.split("New-Project/")[-1]
    return PROJECT / p


def collect_str_replaces(session_id: str) -> list[dict]:
    jsonl = TRANSCRIPTS / session_id / f"{session_id}.jsonl"
    if not jsonl.exists():
        print(f"  skip missing {jsonl}")
        return []
    out = []
    for line in jsonl.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        for part in obj.get("message", {}).get("content", []):
            if part.get("type") != "tool_use" or part.get("name") != "StrReplace":
                continue
            inp = part.get("input", {})
            path = inp.get("path", "")
            old = inp.get("old_string")
            new = inp.get("new_string")
            if path and old is not None and new is not None:
                out.append({"path": path, "old": old, "new": new, "session": session_id})
    return out


def apply_reverse(replaces: list[dict]) -> tuple[int, int, list[str]]:
    ok = 0
    fail = 0
    errors = []
    # Reverse order: undo last change first
    for r in reversed(replaces):
        fp = norm_path(r["path"])
        if not fp.exists():
            errors.append(f"MISSING FILE {fp}")
            fail += 1
            continue
        text = fp.read_text(encoding="utf-8")
        # Reverse: replace new with old
        if r["new"] not in text:
            # try normalized line endings
            new_norm = r["new"].replace("\r\n", "\n")
            text_norm = text.replace("\r\n", "\n")
            if new_norm not in text_norm:
                preview = r["new"][:80].replace("\n", "\\n")
                errors.append(f"NOT FOUND in {fp.name}: {preview}...")
                fail += 1
                continue
            text = text_norm.replace(new_norm, r["old"].replace("\r\n", "\n"), 1)
        else:
            text = text.replace(r["new"], r["old"], 1)
        fp.write_text(text, encoding="utf-8", newline="\n")
        ok += 1
        print(f"  OK {fp.relative_to(PROJECT)}")
    return ok, fail, errors


def main():
    all_rep = []
    for sid in JUN11_SESSIONS:
        reps = collect_str_replaces(sid)
        print(f"{sid}: {len(reps)} StrReplace ops")
        all_rep.extend(reps)

    print(f"\nReversing {len(all_rep)} edits...")
    ok, fail, errors = apply_reverse(all_rep)
    print(f"\nDone: {ok} reversed, {fail} failed")
    if errors:
        print("\nFailures:")
        for e in errors[:30]:
            print(f"  {e}")
        if len(errors) > 30:
            print(f"  ... and {len(errors) - 30} more")


if __name__ == "__main__":
    main()
