"""Writes one copy of qa/round5/scenarios.json per built-in skin, with interface.skin set in the
defaults, so the whole sweep runs under each skin (qa/round7/skins.md).

Usage: python qa/round7/skin-scenarios.py <out folder> [skin ...]
Then:  powershell -File tools\\dev\\sweep.ps1 -Exe <exe> -Scenarios <out>\\scenarios-<skin>.json -Out <dir>
Scenarios that set interface.skin themselves keep their own value.
"""
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[2]
src = root / "qa/round5/scenarios.json"
out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(".")
skins = sys.argv[2:] or ["lumen", "oled", "contrast", "sepia"]
out.mkdir(parents=True, exist_ok=True)
for skin in skins:
    doc = json.loads(src.read_text(encoding="utf-8"))
    doc.setdefault("defaults", {}).setdefault("settings", {})["interface.skin"] = skin
    path = out / f"scenarios-{skin}.json"
    path.write_text(json.dumps(doc, ensure_ascii=False, indent=1), encoding="utf-8")
    print(path)
