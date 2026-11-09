#!/usr/bin/env python3
"""Checks Resources/Lang/fr.txt against the code.

Every English text the interface translates must have a French version, with the same
%1, %2... placeholders. Texts are found in tr ("..."), lang::tr ("...") and TRANS ("...")
calls, plus the few lists that are translated through a variable (page names, generator
modes and families, Tauntek key labels, library kinds, generator section names).

Usage: check_translations.py <project folder>     (exit code 1 on a problem)
"""
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(errors="replace")   # Windows consoles
root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent)
literal = r'"((?:[^"\\]|\\.)*)"'

def unescape(s):
    return s.replace('\\"', '"').replace("\\n", "\n").replace("\\'", "'").replace("\\\\", "\\")

keys = {}
sources = list((root / "Source").rglob("*.cpp")) + list((root / "Source").rglob("*.h"))
for path in sources:
    text = path.read_text(encoding="utf-8")
    for m in re.finditer(r'(?:\btr|TRANS)\s*\(\s*' + literal, text):
        keys[unescape(m.group(1))] = path.name
    # lists translated through a variable
    for name in ("tabNames", "modeNames", "padNames"):
        for block in re.finditer(name + r'\[\]\s*=\s*\{([^}]*)\}', text):
            for m in re.finditer(literal, block.group(1)):
                keys[unescape(m.group(1))] = path.name
    if path.name == "Panels.cpp":
        for m in re.finditer(r'\{\s*\d+,\s*' + literal + r',\s*KeyKind::\w+,\s*(?:' + literal + r'|nullptr)\s*\}', text):
            keys[unescape(m.group(1))] = path.name
            if m.group(2):
                keys[unescape(m.group(2))] = path.name
        for kind in re.findall(r'kind = "([A-Z ]+)"', text) + re.findall(r'\{ f, "([A-Z]+)", f\.', text):
            keys[kind] = path.name
    if path.name == "Generator.cpp":
        for fn in ("sectionName", "familyName"):
            body = re.search(fn + r'\s*\([^)]*\)\s*\{(.*?)\n\}', text, re.S).group(1)
            for m in re.finditer(r'return ' + literal, body):
                if m.group(1):
                    keys[unescape(m.group(1))] = path.name

# errors of the SysEx codec (plain C++, translated where the editor shows them)
core = (root / "Source/core/OscarSysex.cpp").read_text(encoding="utf-8")
for m in re.finditer(literal, core):
    k = unescape(m.group(1))
    if re.match(r"[A-Z].* .*\.$", k) and "%" not in k:   # numbered errors stay in English
        keys[k] = "OscarSysex.cpp"
# messages put together around blockName(): std::string ("Incomplete ") + blockName (type) + " data..."
for m in re.finditer(r'std::string \("([^"]*)"\)\s*\+\s*blockName \(type\)\s*\+\s*"([^"]*)"', core):
    for name in ("waveform", "voice", "sequence"):
        keys[m.group(1) + name + m.group(2)] = "OscarSysex.cpp"

# fr.txt (JUCE LocalisedStrings format)
fr = {}
problems = []
for n, line in enumerate((root / "Resources/Lang/fr.txt").read_text(encoding="utf-8").splitlines(), 1):
    line = line.strip()
    if not line or line.startswith(("language:", "countries:", "#")):
        continue
    m = re.fullmatch(literal + r'\s*=\s*' + literal, line)
    if not m:
        problems.append(f"fr.txt:{n}: not a \"English\" = \"French\" line")
        continue
    k, v = unescape(m.group(1)), unescape(m.group(2))
    if k in fr:
        problems.append(f"fr.txt:{n}: duplicate key {k!r}")
    if not v.strip():
        problems.append(f"fr.txt:{n}: empty translation for {k!r}")
    if sorted(re.findall(r"%\d", k)) != sorted(re.findall(r"%\d", v)):
        problems.append(f"fr.txt:{n}: placeholders differ in {k!r}")
    fr[k] = v

for k, where in sorted(keys.items()):
    if k not in fr:
        problems.append(f"missing translation ({where}): {k!r}")

for k in sorted(set(fr) - set(keys)):
    print(f"note: unused translation {k!r}")

for p in problems:
    print(p)
print(f"{len(keys)} texts used, {len(fr)} translations, {len(problems)} problems")
sys.exit(1 if problems else 0)
