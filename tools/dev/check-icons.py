"""Lists icon names used in the sources that are missing from src/gfx/IconData.inc.

A missing icon draws nothing, so a typo silently leaves an empty spot in the UI.
Usage: python tools/dev/check-icons.py
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2]
known = set(re.findall(r'\{"([a-z0-9_\-]+)",', (root / "src/gfx/IconData.inc").read_text(encoding="utf-8")))
if not known:
    sys.exit("no icons found in IconData.inc")

NAME = r'"([a-z0-9_\-]+)"'
patterns = [
    re.compile(r'Icon\(\s*' + NAME),                                   # p.Icon("x", ...)
    re.compile(r'\bicon\s*=\s*' + NAME),                               # m.icon = "x"
    re.compile(r'<(?:IconView|IconButton|MiniOverlayButton|PanelButton|Button)>\([^;]*?' + NAME + r'\s*[,)]'),
    re.compile(r'\bItem\(L"(?:[^"\\]|\\.)*"\s*,\s*' + NAME),           # Item(L"Label", "x", ...)
    re.compile(r'\b(?:Show)?[Tt]oast\([^;]*,\s*' + NAME),              # Toast(text, "x"), ShowToast(host, text, "x")
]
missing = {}
for path in (root / "src").rglob("*.cpp"):
    text = path.read_text(encoding="utf-8", errors="replace")
    for pat in patterns:
        for m in pat.finditer(text):
            name = m.group(1)
            if name not in known:
                line = text.count("\n", 0, m.start()) + 1
                missing.setdefault(name, []).append(f"{path.relative_to(root)}:{line}")
for name, where in sorted(missing.items()):
    print(f"{name}: {', '.join(where[:4])}{' ...' if len(where) > 4 else ''}")
print(f"{len(missing)} missing of {len(known)} known icons")
sys.exit(1 if missing else 0)
