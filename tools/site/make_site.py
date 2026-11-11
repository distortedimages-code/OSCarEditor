#!/usr/bin/env python3
"""Builds the OXIde web page served by GitHub Pages, in English (docs/index.html) and
French (docs/fr.html), from template.html.

The template holds both languages side by side as [[fr]]texte[[en]]text[[/]]; the lists below
(plugin pages, shortcut keys...) have their own translations. Edit them, then run:
    python3 tools/site/make_site.py [preview folder]
The pictures are screenshots of the plugin (docs/img for French, docs/img/en for English). The
latest releases are read live from GitHub by the page itself, so publishing a release needs no
change here; the notes built into the page come from RELEASE_NOTES.md."""
import json
import math
import re
import sys
from pathlib import Path
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "logo"))
import make_logo as logo   # noqa: E402  (the logo, as the plug-in draws it)
sys.path.insert(0, str(Path(__file__).resolve().parent))
import guide   # noqa: E402  (the hands-on guide and its drawings)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
REPO = "distortedimages-code/OSCarEditor"
GH = f"https://github.com/{REPO}"
DOWNLOAD = f"{GH}/releases/latest/download/OXIde-mac.dmg"
KOFI = "https://ko-fi.com/amnesy"
KOFI_EMBED = "https://ko-fi.com/amnesy/?hidefeed=true&widget=true&embed=true&preview=true"
TAUNTEK = "http://www.tauntek.com/OSCar.htm"
VERSION = re.search(r"project\(OSCarEditor VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
NB = "\u00a0"

ISSUE = {
    "fr": {"bug": ("Bug : ", """**Version du plugin :** (en haut de la page Réglages)
**macOS :**
**DAW et format (AU, VST3 ou application) :**

**Ce qui se passe :**

**Ce qui devrait se passer :**

**Le journal (page Réglages), si possible :**
"""), "idea": ("Idée : ", """**Ton idée :**

**Dans quelle situation elle t'aiderait :**
""")},
    "en": {"bug": ("Bug: ", """**Plug-in version:** (top of the Setup page)
**macOS:**
**DAW and format (AU, VST3 or app):**

**What happens:**

**What should happen:**

**The log (Setup page), if you can:**
"""), "idea": ("Idea: ", """**Your idea:**

**When it would help you:**
""")},
}

# The plugin's seven pages: picture, name, caption
PAGES = {
    "fr": [
        ("voix", "Voix", "Tous les réglages de la voix choisie. Chaque potard part en CC vers l'OSCar, et tout s'automatise depuis le DAW."),
        ("generateur", "Générateur", "Varier, Nouveau (basse, lead, pad, percu, texture) et Croiser, avec des verrous par section. Chaque essai joue sur l'OSCar sans toucher à sa mémoire."),
        ("morphing", "Morphing", "Deux voix A et B et un crossfader : les potards glissent de l'une à l'autre, les sélecteurs basculent à mi-course. Un lissage contre les gestes brusques, et le fader s'automatise dans Logic."),
        ("banques", "Banques", "Les 36 voix avec leurs noms, et la bibliothèque : tes banques, tes voix et toutes les sauvegardes automatiques."),
        ("formes-d-onde", "Formes d'onde", "Les cinq formes d'onde utilisateur (−8 à −12), à dessiner à la souris."),
        ("raccourcis-tauntek", "Raccourcis Tauntek", "Tous les raccourcis TUNE/TRANSPOSE sur le clavier. Les touches qui remplacent les voix en mémoire sont en rouge."),
        ("reglages", "Réglages", "Ports MIDI, langue, Live CC, horloge MIDI, MIDI Thru, mises à jour et journal."),
    ],
    "en": [
        ("voix", "Voice", "Every setting of the chosen voice. Each knob goes to the OSCar as a CC, and everything can be automated from the DAW."),
        ("generateur", "Generator", "Vary, New (bass, lead, pad, percussive, texture) and Breed, with locks per section. Every try plays on the OSCar without touching its memory."),
        ("morphing", "Morph", "Two voices A and B and a crossfader: knobs glide from one to the other, switches change half way. Smoothing guards against sudden moves, and the fader can be automated in Logic."),
        ("banques", "Banks", "The 36 voices with their names, and the library: your banks, your voices and every automatic backup."),
        ("formes-d-onde", "Waveforms", "The five user waveforms (−8 to −12), drawn with the mouse."),
        ("raccourcis-tauntek", "Tauntek keys", "Every TUNE/TRANSPOSE shortcut on the keyboard. The keys that replace the voices in memory are red."),
        ("reglages", "Setup", "MIDI ports, language, Live CC, MIDI clock, MIDI Thru, updates and log."),
    ],
}
PAGE_ALT = {"fr": "OXIde, page {}", "en": "OXIde, {} page"}

# Tauntek shortcut keys: hold TUNE/TRANSPOSE, press a key (key 0 = lowest C). From Tauntek's
# control-key chart, as on the plugin's Tauntek keys page.
# Kind: n = setting, r = replaces the 36 voices, s = overwrites a save area.
SHORTCUTS = [   # key, kind, group, French, English
    (0,  "r", "memory", "Charger la zone de sauvegarde 1",  "Load patches from save area 1"),
    (1,  "s", "memory", "Sauver dans la zone 1",            "Save patches to save area 1"),
    (2,  "r", "memory", "Charger la zone de sauvegarde 2",  "Load patches from save area 2"),
    (3,  "s", "memory", "Sauver dans la zone 2",            "Save patches to save area 2"),
    (23, "r", "memory", "Charger les sons d'usine",         "Load factory patches"),
    (24, "r", "memory", "Charger les sons B",               "Load B patches"),
    (25, "r", "memory", "Charger les sons C",               "Load C patches"),
    (22, "n", "dump",   "Activer le patch CC dump",         "Enable patch CC dump"),
    (21, "n", "dump",   "Désactiver le patch CC dump",      "Disable patch CC dump"),
    (20, "n", "play",   "Arpège aléatoire",                 "Random arp mode"),
    (26, "n", "play",   "Vélocité vers l'enveloppe du VCF", "Velocity to VCF envelope amount"),
    (27, "n", "play",   "Vélocité ignorée",                 "Velocity not used"),
    (29, "n", "pwm",    "PWM la plus lente",                "PWM slowest rate"),
    (30, "n", "pwm",    "PWM plus lente",                   "PWM slower rate"),
    (31, "n", "pwm",    "PWM normale",                      "PWM normal rate"),
    (32, "n", "pwm",    "PWM la plus rapide",               "PWM fastest rate"),
    (34, "n", "lfo",    "Activer la synchro LFO",           "Enable LFO sync"),
    (33, "n", "lfo",    "Couper la synchro LFO",            "Disable LFO sync"),
    (35, "n", "lfo",    "Activer LFO vers oscillateur 1",   "Enable LFO to oscillator 1"),
    (36, "n", "lfo",    "Couper LFO vers oscillateur 1",    "Disable LFO to oscillator 1"),
]
GROUP_NAMES = {
    "fr": {"memory": "sons en mémoire", "dump": "patch cc dump", "play": "jeu", "pwm": "pwm", "lfo": "lfo"},
    "en": {"memory": "voices in memory", "dump": "patch cc dump", "play": "playing", "pwm": "pwm", "lfo": "lfo"},
}
COLUMNS = [["memory"], ["dump", "pwm"], ["play", "lfo"]]   # three balanced columns
NOTES = {
    "fr": ["Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"],
    "en": ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"],
}

TEXT = {
    "fr": {"copy": "Copier", "copied": "Copié", "selected": "Sélectionné", "earlier": "Versions précédentes",
           "latest": "dernière version",
           "months": ["janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre"]},
    "en": {"copy": "Copy", "copied": "Copied", "selected": "Selected", "earlier": "Earlier versions",
           "latest": "latest version",
           "months": ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]},
}

CABLING_TEXT = {
    "fr": {"label": "Câble 1 : sortie MIDI de l'interface vers l'entrée MIDI de l'OSCar. Câble 2 : sortie MIDI de l'OSCar vers l'entrée MIDI de l'interface.",
           "iface": "Interface MIDI", "oscar": "OSCar, face arrière", "c1": "câble 1", "c2": "câble 2"},
    "en": {"label": "Cable 1: the interface's MIDI output to the OSCar's MIDI input. Cable 2: the OSCar's MIDI output to the interface's MIDI input.",
           "iface": "MIDI interface", "oscar": "OSCar, rear panel", "c1": "cable 1", "c2": "cable 2"},
}


# ---------------------------------------------------------------- drawings
def din(cx, cy, r=15):
    pins = "".join(
        f'<circle class="pin" cx="{cx + math.cos(math.radians(a)) * r * 0.55:.1f}" cy="{cy - math.sin(math.radians(a)) * r * 0.55 + 2:.1f}" r="1.7"/>'
        for a in (180, 135, 90, 45, 0))
    return f'<circle class="din" cx="{cx}" cy="{cy}" r="{r}"/>{pins}'



def cabling_svg(lang):
    t = CABLING_TEXT[lang]
    return f'''<svg class="cabling" viewBox="0 30 700 230" role="img" aria-label="{t["label"]}">
  <defs>
    <marker id="arrowA" viewBox="0 0 10 10" refX="7" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" class="ma"/></marker>
    <marker id="arrowB" viewBox="0 0 10 10" refX="7" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" class="mb"/></marker>
  </defs>
  <rect class="dev" x="22" y="56" width="112" height="70" rx="6"/>
  <rect class="screen" x="31" y="64" width="94" height="54" rx="2"/>
  <path class="dev" d="M10,132 L146,132 L138,140 L18,140 Z"/>
  <text class="dl" x="78" y="162" text-anchor="middle">Mac</text>
  <path class="usb" d="M146,100 L250,100"/>
  <text class="sl" x="198" y="91" text-anchor="middle">USB</text>
  <rect class="dev" x="250" y="52" width="164" height="104" rx="10"/>
  <text class="dl" x="332" y="76" text-anchor="middle">{t["iface"]}</text>
  <text class="pl" x="297" y="101" text-anchor="middle">MIDI OUT</text>
  <text class="pl" x="367" y="101" text-anchor="middle">MIDI IN</text>
  {din(297, 128)}{din(367, 128)}
  <rect class="dev oscar" x="486" y="42" width="200" height="120" rx="8"/>
  <text class="dl" x="586" y="66" text-anchor="middle">{t["oscar"]}</text>
  <text class="pl" x="550" y="101" text-anchor="middle">MIDI IN</text>
  <text class="pl" x="622" y="101" text-anchor="middle">MIDI OUT</text>
  {din(550, 128)}{din(622, 128)}
  <path class="c1" d="M297,144 C297,212 550,212 550,148" marker-end="url(#arrowA)"/>
  <rect class="t1" x="380" y="186" width="86" height="24" rx="12"/>
  <text class="tt1" x="423" y="202" text-anchor="middle">{t["c1"]}</text>
  <path class="c2" d="M622,144 C622,252 367,252 367,148" marker-end="url(#arrowB)"/>
  <rect class="t2" x="452" y="218" width="86" height="24" rx="12"/>
  <text class="tt2" x="495" y="234" text-anchor="middle">{t["c2"]}</text>
</svg>'''


def keyboard_svg(highlight=None, callout=None, labels=(), kinds=None):
    """37 keys, C to C. Key 0 is the lowest C (the OSCar's key -12).
    highlight + callout: one key to press, named under the keyboard.
    kinds: {key: 'n' | 'r' | 's'} colours the Tauntek shortcut keys and makes them clickable (data-k)."""
    kinds = kinds or {}
    black = lambda k: k % 12 in (1, 3, 6, 8, 10)
    whites = [k for k in range(37) if not black(k)]
    top = 46 if labels else 8
    x0, y0, ww, wh = 10, top, 30, 120
    pos, out = {}, []

    def attrs(k, base):
        if k in kinds:
            return f'class="{base} sk {kinds[k]}" data-k="{k}"'
        return f'class="{base}{" on" if k == highlight else ""}"'

    for i, k in enumerate(whites):
        x = x0 + i * ww
        pos[k] = x + ww / 2
        out.append(f'<rect {attrs(k, "kw")} x="{x}" y="{y0}" width="{ww}" height="{wh}" rx="2"/>')
    bw, bh = ww * 0.62, wh * 0.62
    for k in range(37):
        if black(k):
            x = x0 + (whites.index(k - 1) + 1) * ww - bw / 2
            pos[k] = x + bw / 2
            out.append(f'<rect {attrs(k, "kb")} x="{x:.1f}" y="{y0}" width="{bw:.1f}" height="{bh:.1f}" rx="2"/>')
    for k, t in labels:
        on = " on" if k == highlight else ""
        out.append(f'<text class="kl{on}" x="{pos[k]:.1f}" y="{y0 - 10}" text-anchor="middle">{t}</text>')
    height = y0 + wh + 8
    if callout:
        cx = pos[highlight]
        ky = y0 + (bh if black(highlight) else wh) - 14
        out.append(f'<circle class="dot" cx="{cx:.1f}" cy="{ky:.1f}" r="5"/>')
        out.append(f'<path class="lead" d="M{cx:.1f},{ky:.1f} L{cx:.1f},{y0 + wh + 26}"/>')
        anchor = "start" if cx < 340 else "end"
        tx = cx - 8 if anchor == "end" else cx + 8
        out.append(f'<text class="co" x="{tx:.1f}" y="{y0 + wh + 30}" text-anchor="{anchor}">{callout}</text>')
        height = y0 + wh + 44
    w = x0 * 2 + len(whites) * ww
    label = "Clavier de l'OSCar, touches raccourcis en couleur" if kinds else "Clavier de l'OSCar, touche à presser en couleur"
    return f'<svg class="kbd" viewBox="0 0 {w} {height}" role="img" aria-label="{label}">' + "".join(out) + "</svg>"


# ---------------------------------------------------------------- page parts
def key_name(k, lang, short=False):
    """Where a key is on the 37-key keyboard: note and octave (1 = lowest; key 36 is the top C)."""
    if k == 36:
        return "Do aigu" if lang == "fr" else "top C"
    note = NOTES[lang][k % 12]
    return f"{note} {k // 12 + 1}" if short else f"{note} · octave {k // 12 + 1}"


def label_of(item, lang):
    return item[3] if lang == "fr" else item[4]


def shortcuts_html(lang):
    cols = []
    for column in COLUMNS:
        groups = []
        for group in column:
            lis = "".join(
                f'<li><button type="button" class="sc {x[1]}" data-k="{x[0]}"><span class="sk-sw" aria-hidden="true"></span>'
                f'<span class="sc-label">{label_of(x, lang)}</span><span class="sc-key">{key_name(x[0], lang, short=True)}</span></button></li>'
                for x in SHORTCUTS if x[2] == group)
            groups.append(f'<div class="sc-group"><h3 class="mini-legend">{GROUP_NAMES[lang][group]}</h3><ul>{lis}</ul></div>')
        cols.append('<div class="sc-col">' + "".join(groups) + "</div>")
    return "\n".join(cols)


def release_notes(lang):
    """The notes built into the page: RELEASE_NOTES.md, in this page's language."""
    md = (ROOT / "RELEASE_NOTES.md").read_text(encoding="utf-8")
    parts = re.split(r"^##\s+(English|Fran(?:ç|c)ais)\s*$", md, flags=re.M)
    if len(parts) >= 5:
        md = next(parts[i + 1] for i in range(1, len(parts), 2) if (lang == "fr") == parts[i].startswith("Fran"))
    paras = []
    for block in re.split(r"\n{2,}", md.strip()):
        block = " ".join(l.strip() for l in block.splitlines() if l.strip())
        if not block or block.startswith("#"):
            continue
        block = block.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        block = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", block)
        block = re.sub(r"`(.+?)`", r"<code>\1</code>", block)
        paras.append(f"        <p>{block}</p>")
    return (f'    <article class="release">\n      <div><span class="vchip">{VERSION}</span><div class="cap when">{TEXT[lang]["latest"]}</div></div>\n'
            f'      <div class="notes">\n' + "\n".join(paras) + "\n      </div>\n    </article>")


def french_spacing(text):
    """Non-breaking spaces before : ; ? ! and inside guillemets, in text only (not in tags)."""
    parts = re.split(r"(<[^>]*>)", text)
    for i in range(0, len(parts), 2):
        t = parts[i].replace("« ", "«" + NB).replace(" »", NB + "»")
        parts[i] = re.sub(r" ([:;?!])(?=\s|$)", NB + r"\1", t)
    return "".join(parts)


def build(lang):
    html = (HERE / "template.html").read_text(encoding="utf-8")
    pick = (lambda m: french_spacing(m.group(1))) if lang == "fr" else (lambda m: m.group(2))
    html = re.sub(r"\[\[fr\]\](.*?)\[\[en\]\](.*?)\[\[/\]\]", pick, html, flags=re.S)
    assert "[[" not in html, html[html.index("[["):html.index("[[") + 60]

    pages = PAGES[lang]
    title, body = ISSUE[lang]["bug"]
    idea_title, idea_body = ISSUE[lang]["idea"]
    first = next(x for x in SHORTCUTS if x[0] == 22)
    values = {
        "{{GH}}": GH, "{{DOWNLOAD}}": DOWNLOAD, "{{KOFI}}": KOFI, "{{KOFI_EMBED}}": KOFI_EMBED, "{{TAUNTEK}}": TAUNTEK,
        "{{REPO}}": REPO, "{{LANG}}": lang, "{{VERSION}}": VERSION,
        "{{ON_EN}}": " on" if lang == "en" else "", "{{ON_FR}}": " on" if lang == "fr" else "",
        "{{IMG}}": "img/" if lang == "fr" else "img/en/",
        "{{BUG_URL}}": f"{GH}/issues/new?labels=bug&title={quote(title)}&body={quote(body)}",
        "{{IDEA_URL}}": f"{GH}/issues/new?labels=enhancement&title={quote(idea_title)}&body={quote(idea_body)}",
        "{{CABLING}}": cabling_svg(lang),
        "{{LOGO}}": logo.svg("currentColor", "logo"),
        "{{KB_PC}}": keyboard_svg(3, "−9 : la 2e touche noire" if lang == "fr" else "−9: the 2nd black key", [(0, "−12"), (3, "−9"), (36, "24")]),
        "{{KB_DUMP}}": keyboard_svg(22, "Enable patch CC dump", [(0, "−12"), (36, "24")]),
        "{{KB_KEYS}}": keyboard_svg(kinds={x[0]: x[1] for x in SHORTCUTS}),
        "{{SHORTCUTS}}": shortcuts_html(lang),
        "{{SHORTCUTS_JS}}": json.dumps({str(x[0]): [label_of(x, lang), key_name(x[0], lang), x[1]] for x in SHORTCUTS}, ensure_ascii=False),
        "{{RO_LABEL}}": label_of(first, lang).upper(), "{{RO_KEY}}": key_name(22, lang),
        "{{PAGE_BUTTONS}}": "\n".join(
            f'<button type="button" class="mini{" on" if i == 0 else ""}" id="shot-{slug}" data-i="{i}" aria-pressed="{"true" if i == 0 else "false"}">'
            f'<span>{name}</span><i class="led"></i></button>' for i, (slug, name, _) in enumerate(pages)),
        "{{PAGES_JS}}": json.dumps([[slug, name, cap, PAGE_ALT[lang].format(name)] for slug, name, cap in pages], ensure_ascii=False),
        "{{FIRST_CAPTION}}": pages[0][2], "{{FIRST_ALT}}": PAGE_ALT[lang].format(pages[0][1]),
        "{{STATIC_RELEASES}}": release_notes(lang),
        "{{GUIDE}}": french_spacing(guide.guide_html(lang)) if lang == "fr" else guide.guide_html(lang),
        "{{JS_TEXT}}": json.dumps(TEXT[lang], ensure_ascii=False),
    }
    for k, v in values.items():
        html = html.replace(k, v)
    assert "{{" not in html, html[html.index("{{"):html.index("{{") + 40]
    return html


def full_page(fragment, lang):
    """GitHub Pages serves the file as is: give it a document around the fragment."""
    head_end = fragment.index("</style>") + len("</style>")
    return ("<!doctype html>\n" f'<html lang="{lang}">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
            + fragment[:head_end] + "\n</head>\n<body>\n" + fragment[head_end:].strip() + "\n</body>\n</html>\n")


if __name__ == "__main__":
    docs = ROOT / "docs"
    pages = {lang: build(lang) for lang in ("en", "fr")}
    (docs / "index.html").write_text(full_page(pages["en"], "en"), encoding="utf-8")
    (docs / "fr.html").write_text(full_page(pages["fr"], "fr"), encoding="utf-8")
    print("docs/index.html (en), docs/fr.html (fr)", VERSION)
    if len(sys.argv) > 1:   # a preview copy for a Claude artifact: the main page is a fragment
        out = Path(sys.argv[1])
        (out / "index.html").write_text(pages["en"], encoding="utf-8")
        (out / "fr.html").write_text(full_page(pages["fr"], "fr"), encoding="utf-8")
        print("preview in", out)
