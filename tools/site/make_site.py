#!/usr/bin/env python3
"""Builds docs/index.html, the OSCar Editor web page served by GitHub Pages, from template.html.

Edit template.html (texts, styles) or the lists below, then run:  python3 tools/site/make_site.py
The pictures in docs/img are screenshots of the plugin. The latest releases are read live
from GitHub by the page itself, so publishing a release needs no change here."""
import math
from pathlib import Path
from urllib.parse import quote

REPO = "distortedimages-code/OSCarEditor"
GH = f"https://github.com/{REPO}"
DOWNLOAD = f"{GH}/releases/latest/download/OSCar-Editor-mac.zip"
KOFI = "https://ko-fi.com/amnesy"
TAUNTEK = "http://www.tauntek.com/OSCar.htm"

BUG_BODY = """**Version du plugin :** (en haut de la page Réglages)
**macOS :**
**DAW et format (AU, VST3 ou application) :**

**Ce qui se passe :**

**Ce qui devrait se passer :**

**Le journal (page Réglages), si possible :**
"""
IDEA_BODY = """**Ton idée :**

**Dans quelle situation elle t'aiderait :**
"""
BUG_URL = f"{GH}/issues/new?labels=bug&title={quote('Bug : ')}&body={quote(BUG_BODY)}"
IDEA_URL = f"{GH}/issues/new?labels=enhancement&title={quote('Idée : ')}&body={quote(IDEA_BODY)}"


# ---------------------------------------------------------------- Tauntek shortcut keys
# Hold TUNE/TRANSPOSE, press a key (key 0 = lowest C). From Tauntek's control-key chart, as on the
# plugin's "Raccourcis Tauntek" page. Kind: n = normal, r = replaces the 36 voices, s = overwrites a save area.
GROUPS = ["Sons en mémoire", "Patch CC dump", "Jeu", "PWM", "LFO"]
SHORTCUTS = [
    (0,  "Charger la zone de sauvegarde 1", "r", "Sons en mémoire"),
    (1,  "Sauver dans la zone 1",           "s", "Sons en mémoire"),
    (2,  "Charger la zone de sauvegarde 2", "r", "Sons en mémoire"),
    (3,  "Sauver dans la zone 2",           "s", "Sons en mémoire"),
    (23, "Charger les sons d'usine",        "r", "Sons en mémoire"),
    (24, "Charger les sons B",              "r", "Sons en mémoire"),
    (25, "Charger les sons C",              "r", "Sons en mémoire"),
    (22, "Activer le patch CC dump",        "n", "Patch CC dump"),
    (21, "Désactiver le patch CC dump",     "n", "Patch CC dump"),
    (20, "Arpège aléatoire",                "n", "Jeu"),
    (26, "Vélocité vers l'enveloppe du VCF", "n", "Jeu"),
    (27, "Vélocité ignorée",                "n", "Jeu"),
    (29, "PWM la plus lente",               "n", "PWM"),
    (30, "PWM plus lente",                  "n", "PWM"),
    (31, "PWM normale",                     "n", "PWM"),
    (32, "PWM la plus rapide",              "n", "PWM"),
    (34, "Activer la synchro LFO",          "n", "LFO"),
    (33, "Couper la synchro LFO",           "n", "LFO"),
    (35, "Activer LFO vers oscillateur 1",  "n", "LFO"),
    (36, "Couper LFO vers oscillateur 1",   "n", "LFO"),
]


# ---------------------------------------------------------------- drawings
def din(cx, cy, r=15):
    pins = "".join(
        f'<circle class="pin" cx="{cx + math.cos(math.radians(a)) * r * 0.55:.1f}" cy="{cy - math.sin(math.radians(a)) * r * 0.55 + 2:.1f}" r="1.7"/>'
        for a in (180, 135, 90, 45, 0))
    return f'<circle class="din" cx="{cx}" cy="{cy}" r="{r}"/>{pins}'


CABLING = f'''<svg class="cabling" viewBox="0 30 700 230" role="img" aria-label="Câble 1 : sortie MIDI de l'interface vers l'entrée MIDI de l'OSCar. Câble 2 : sortie MIDI de l'OSCar vers l'entrée MIDI de l'interface.">
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
  <text class="dl" x="332" y="76" text-anchor="middle">Interface MIDI</text>
  <text class="pl" x="297" y="101" text-anchor="middle">MIDI OUT</text>
  <text class="pl" x="367" y="101" text-anchor="middle">MIDI IN</text>
  {din(297, 128)}{din(367, 128)}
  <rect class="dev oscar" x="486" y="42" width="200" height="120" rx="8"/>
  <text class="dl" x="586" y="66" text-anchor="middle">OSCar, face arrière</text>
  <text class="pl" x="550" y="101" text-anchor="middle">MIDI IN</text>
  <text class="pl" x="622" y="101" text-anchor="middle">MIDI OUT</text>
  {din(550, 128)}{din(622, 128)}
  <path class="c1" d="M297,144 C297,212 550,212 550,148" marker-end="url(#arrowA)"/>
  <rect class="t1" x="380" y="186" width="86" height="24" rx="12"/>
  <text class="tt1" x="423" y="202" text-anchor="middle">câble 1</text>
  <path class="c2" d="M622,144 C622,252 367,252 367,148" marker-end="url(#arrowB)"/>
  <rect class="t2" x="452" y="218" width="86" height="24" rx="12"/>
  <text class="tt2" x="495" y="234" text-anchor="middle">câble 2</text>
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


NOTES = ["Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"]


def key_name(k, short=False):
    """Where a key is on the 37-key keyboard: note and octave (1 = lowest; key 36 is the top C)."""
    if k == 36:
        return "Do aigu"
    return f"{NOTES[k % 12]} {k // 12 + 1}" if short else f"{NOTES[k % 12]} · octave {k // 12 + 1}"


KB_PC = keyboard_svg(3, "−9 : la 2e touche noire", [(0, "−12"), (3, "−9"), (36, "24")])
KB_DUMP = keyboard_svg(22, "Enable patch CC dump", [(0, "−12"), (36, "24")])
KB_KEYS = keyboard_svg(kinds={k: kind for k, _, kind, _ in SHORTCUTS})


# the themes, laid out in three balanced columns
COLUMNS = [["Sons en mémoire"], ["Patch CC dump", "PWM"], ["Jeu", "LFO"]]


def shortcuts_html():
    cols = []
    for column in COLUMNS:
        groups = []
        for group in column:
            items = [x for x in SHORTCUTS if x[3] == group]
            lis = "".join(
                f'<li><button type="button" class="sc {kind}" data-k="{k}"><span class="sk-sw" aria-hidden="true"></span>'
                f'<span class="sc-label">{label}</span><span class="sc-key">{key_name(k, short=True)}</span></button></li>'
                for k, label, kind, _ in items)
            groups.append(f'<div class="sc-group"><h3 class="mini-legend">{group.lower()}</h3><ul>{lis}</ul></div>')
        cols.append('<div class="sc-col">' + "".join(groups) + "</div>")
    return "\n".join(cols)




PAGES = [
    ("voix", "Voix", "Tous les réglages de la voix choisie. Chaque potard part en CC vers l'OSCar, et tout s'automatise depuis le DAW."),
    ("generateur", "Générateur", "Varier, Nouveau (basse, lead, pad, percu, texture) et Croiser, avec des verrous par section. Chaque essai joue sur l'OSCar sans toucher à sa mémoire."),
    ("banques", "Banques", "Les 36 voix avec leurs noms, et la bibliothèque : tes banques, tes voix et toutes les sauvegardes automatiques."),
    ("formes-d-onde", "Formes d'onde", "Les cinq formes d'onde utilisateur (−8 à −12), à dessiner à la souris."),
    ("raccourcis-tauntek", "Raccourcis Tauntek", "Tous les raccourcis TUNE/TRANSPOSE sur le clavier. Les touches qui remplacent les voix en mémoire sont en rouge."),
    ("reglages", "Réglages", "Ports MIDI, langue, Live CC, horloge MIDI, MIDI Thru, mises à jour et journal."),
]

page_buttons = "\n".join(
    f'<button type="button" class="mini{" on" if i == 0 else ""}" id="shot-{slug}" data-i="{i}" aria-pressed="{"true" if i == 0 else "false"}">'
    f'<span>{name}</span><i class="led"></i></button>' for i, (slug, name, _) in enumerate(PAGES))
pages_js = ",\n  ".join(f'["{slug}", "{name}", "{cap}"]' for slug, name, cap in PAGES)

HERE = Path(__file__).resolve().parent
html = (HERE / "template.html").read_text(encoding="utf-8")
for k, v in {
    "{{GH}}": GH, "{{DOWNLOAD}}": DOWNLOAD, "{{KOFI}}": KOFI, "{{TAUNTEK}}": TAUNTEK,
    "{{BUG_URL}}": BUG_URL, "{{IDEA_URL}}": IDEA_URL, "{{REPO}}": REPO,
    "{{CABLING}}": CABLING, "{{KB_PC}}": KB_PC, "{{KB_DUMP}}": KB_DUMP, "{{KB_KEYS}}": KB_KEYS, "{{SHORTCUTS}}": shortcuts_html(),
    "{{SHORTCUTS_JS}}": ",".join(f'"{k}":["{label}","{key_name(k)}","{kind}"]' for k, label, kind, _ in SHORTCUTS),
    "{{PAGE_BUTTONS}}": page_buttons, "{{PAGES_JS}}": pages_js,
    "{{FIRST_CAPTION}}": PAGES[0][2],
}.items():
    html = html.replace(k, v)
assert "{{" not in html, html[html.index("{{"):html.index("{{") + 40]
(HERE.parent.parent / "docs" / "index.html").write_text(html, encoding="utf-8")
print(len(html), "bytes")
