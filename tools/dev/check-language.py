"""Checks a tree dump (tools/dev/dump-tree.ps1 -Out) for strings left in the other language.

  python tools/dev/check-language.py dump.txt en|ru [--hidden] [ignored string...]
  python tools/dev/check-language.py          (glossary only: the UI strings in the sources)

Without a dump it checks the glossary: one word per concept in every UI string (strings/*.inc and
the settings schema), so pairs like щелчок / клик stay unified.

Fails on every shown text or tooltip that is a UI string of the other language: a Russian
translation while the UI is English, an English source that has a translation while it is
Russian (also with a key appended, "Stop (S)"). Hidden widgets are skipped (they translate
when they are painted) unless --hidden is given: right after start nothing may be left. Other strings in the wrong script are listed for a look (file names,
formatted strings). Ignored strings are user data that happens to match (a folder named "Videos").
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2] / "src" / "app" / "strings"
pair = re.compile(r'\{L"((?:[^"\\]|\\.)*)",\s*L"((?:[^"\\]|\\.)*)"\}')

# One form per concept (UI review UX-23): (pattern of the wrong form, the form to use).
GLOSSARY = [
    (r"(?<![А-Яа-яЁё])проводник", "Проводник: the Windows name, capitalised"),
    (r"(?<![А-Яа-яЁё])[Кк]лик", "щелчок, as in Windows"),
    (r"[Нн]е удаётся|[Нн]е удается", "Не удалось: one tense for failures"),
    (r"[Уу]далить из (истории|очереди|плейлиста|медиатеки)", "Убрать из …: the file stays; «Удалить» destroys"),
    (r"Недавних файлов нет", "История пуста: the section is «История»"),
    (r"^(Предыдущий|Следующий)$", "Предыдущий файл / Следующий файл, as in the menus"),
    (r"(Settings|Настройки) >", "› between the parts of a path"),
]


def check_glossary():
    literal = re.compile(r'L"((?:[^"\\]|\\.)*)"')
    sources = sorted(root.glob("*.inc")) + [root.parents[1] / "core" / "SettingsSchema.cpp"]
    bad = 0
    for f in sources:
        for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            if line.lstrip().startswith("//"):
                continue
            for s in literal.findall(line):
                for pattern, use in GLOSSARY:
                    if re.search(pattern, s):
                        bad += 1
                        print(f"{f.name}:{n}: {s!r} -> {use}")
    print(f"{bad} glossary mismatches")
    return bad


if len(sys.argv) < 2:
    sys.exit(1 if check_glossary() else 0)
en_to_ru = {}
for f in sorted(root.glob("*.inc")):
    text = "\n".join(l for l in f.read_text(encoding="utf-8").splitlines() if not l.lstrip().startswith("//"))  # header examples
    for m in pair.finditer(text):
        en, ru = (s.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\") for s in m.groups())
        if en != ru and len(en) > 1 and re.search(r"[A-Za-z]", en):  # not units such as "B"
            en_to_ru[en] = ru
hidden_too = "--hidden" in sys.argv
wrong = (set(en_to_ru.values()) if sys.argv[2] == "en" else set(en_to_ru)) - set(sys.argv[3:])
other_script = re.compile(r"[Ѐ-ӿ]" if sys.argv[2] == "en" else r"^[^Ѐ-ӿ]*[A-Za-z]{3}[^Ѐ-ӿ]*$")
field = re.compile(r'(text|tip)="((?:[^"\\]|\\.)*)"')


def is_wrong(s):
    return s in wrong or any(s.startswith(w + " (") for w in wrong if len(w) > 2)


bad, look = [], []
hidden_at = None  # indent of the hidden subtree being skipped
for line in pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig").splitlines():
    if line.startswith("===") or line.startswith("--- popup"):
        hidden_at = None
        continue
    indent = len(line) - len(line.lstrip(" "))
    if hidden_at is not None and indent > hidden_at:
        continue
    hidden_at = indent if not hidden_too and re.search(r"\] hidden\b", line) else None
    if hidden_at is not None:
        continue
    for kind, value in field.findall(line):
        value = value.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")
        if is_wrong(value):
            bad.append(f"{kind}: {value!r}   <- {line.strip()[:80]}")
        elif other_script.search(value):
            look.append(f"{kind}: {value!r}")
for s in sorted(set(look)):
    print("look", s)
for s in bad:
    print("WRONG", s)
print(f"{len(bad)} wrong-language strings ({sys.argv[2]})")
sys.exit(1 if bad else 0)
