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

def panel_svg(mode):
    """Simplified OSCar top panel: where the buttons and keys of a procedure are, numbered in order.
    mode 'pc': SPACE + key -9; 'cc': TUNE/TRANSPOSE + 'Enable patch CC dump' (key 22);
    'dump': INSERT + VOICE, then SPACE; 'keys': TUNE/TRANSPOSE + every Tauntek shortcut key,
    coloured by kind and clickable (data-k), for the shortcuts section."""
    hl = {"pc": {"space": 1, "key": 2}, "cc": {"tune": 1, "key": 2},
          "dump": {"insert": 1, "voice": 2, "space": 3}, "keys": {"tune": 1}}[mode]
    verbs = {"pc": {1: "maintiens", 2: "appuie"}, "cc": {1: "maintiens", 2: "appuie"},
             "dump": {1: "maintiens", 2: "appuie", 3: "puis appuie"}, "keys": {1: "maintiens"}}[mode]
    kinds = {k: kind for k, _, kind, _ in SHORTCUTS} if mode == "keys" else {}

    def key_attrs(k, base):
        if k in kinds:
            return f'class="{base} sk {kinds[k]}" data-k="{k}"'
        return f'class="{base}{" on" if k == key else ""}"'
    key = {"pc": 3, "cc": 22}.get(mode)

    def btn(cx, cy, label, name, show_label=True):
        on = name in hl
        ring = f'<circle class="ring" cx="{cx}" cy="{cy}" r="16"/>' if on else ""
        text = (f'<text class="bl{" on" if on else ""}" x="{cx}" y="{cy + 27}" text-anchor="middle">{label}</text>'
                if show_label or on else "")
        return f'{ring}<circle class="bt{" on" if on else ""}" cx="{cx}" cy="{cy}" r="10"/>{text}'

    def section(x, w, title, active):
        c = "" if active else " dim"
        return (f'<rect class="sec{c}" x="{x}" y="26" width="{w}" height="82" rx="6"/>'
                f'<rect class="secl{c}" x="{x + 10}" y="100" width="{w - 20}" height="15" rx="3"/>'
                f'<text class="sect" x="{x + w / 2}" y="111" text-anchor="middle">{title}</text>')

    lines, key_lines = [], []   # under the buttons and labels / over the keys; the numbered badges on top

    def mark(n, cx, cy, dx, dy, over_keys=False):
        bx, by = cx + dx, cy + dy
        verb = verbs[n]
        tw = 10 + len(verb) * 6.6
        (key_lines if over_keys else lines).append(f'<line class="ml" x1="{cx:.1f}" y1="{cy}" x2="{bx:.1f}" y2="{by}"/>')
        return (f'<circle class="mb" cx="{bx:.1f}" cy="{by}" r="12"/>'
                f'<text class="mn" x="{bx:.1f}" y="{by + 4.5}" text-anchor="middle">{n}</text>'
                f'<rect class="mp" x="{bx + 16:.1f}" y="{by - 10}" width="{tw:.0f}" height="20" rx="10"/>'
                f'<text class="mv" x="{bx + 16 + tw / 2:.1f}" y="{by + 4}" text-anchor="middle">{verb}</text>')

    # 37-key keyboard, key 0 = the OSCar's key -12
    black = lambda k: k % 12 in (1, 3, 6, 8, 10)
    whites = [k for k in range(37) if not black(k)]
    x0, y0, width, height = 176, 176, 500, 104
    ww = width / len(whites)
    keys, pos = [], {}
    for i, k in enumerate(whites):
        x = x0 + i * ww
        pos[k] = x + ww / 2
        keys.append(f'<rect {key_attrs(k, "kw")} x="{x:.1f}" y="{y0}" width="{ww:.1f}" height="{height}"/>')
    bw, bh = ww * 0.62, height * 0.6
    for k in range(37):
        if black(k):
            x = x0 + (whites.index(k - 1) + 1) * ww - bw / 2
            pos[k] = x + bw / 2
            keys.append(f'<rect {key_attrs(k, "kb")} x="{x:.1f}" y="{y0}" width="{bw:.1f}" height="{bh:.1f}" rx="1.5"/>')
    labels = [(0, "−12"), (36, "24")] + ([(3, "−9")] if mode == "pc" else [])
    keylabels = "".join(
        f'<text class="kl{" on" if k == key else ""}" x="{pos[k]:.1f}" y="168" text-anchor="middle">{t}</text>' for k, t in labels)

    space, voice, insert, tune = (128, 230), (288, 58), (468, 58), (58, 58)
    marks = ""
    if "tune" in hl:
        marks += mark(hl["tune"], *tune, 62, 70)
    if "space" in hl:
        marks += mark(hl["space"], *space, -70, 48)
    if "voice" in hl:
        marks += mark(hl["voice"], *voice, -36, 98)
    if "insert" in hl:
        marks += mark(hl["insert"], *insert, 52, 96)
    if key is not None:
        marks += mark(hl["key"], pos[key], 236, 70 if pos[key] < 560 else -150, 34, over_keys=True)
    callout = ""
    if mode == "cc":
        callout = f'<text class="kc" x="{pos[key]:.1f}" y="294" text-anchor="middle">Enable patch CC dump</text>'

    tune_on = "tune" in hl
    return f'''<svg class="panel" viewBox="0 0 700 304" role="img" aria-label="Schéma simplifié du dessus de l'OSCar, boutons à utiliser numérotés dans l'ordre">
  <rect class="body" x="8" y="10" width="684" height="290" rx="12"/>
  {section(26, 104, "TUNE · OSC 2", tune_on)}
  {section(140, 96, "WAVEFORMS", False)}
  {section(246, 130, "STORE AND RECALL", mode == "dump")}
  {section(386, 190, "SEQUENCE EDITING", mode == "dump")}
  {"".join(lines)}
  {btn(*tune, "TUNE/TRANSP.", "tune", show_label=False)}{btn(100, 58, "", "-", show_label=False)}
  {btn(170, 58, "", "-", show_label=False)}{btn(208, 58, "", "-", show_label=False)}
  {btn(*voice, "VOICE", "voice")}{btn(336, 58, "STORE", "-")}
  {btn(420, 58, "REPEAT", "-")}{btn(*insert, "INSERT", "insert")}{btn(516, 58, "DELETE", "-")}{btn(552, 58, "STEP", "-")}
  <rect class="wheel" x="34" y="150" width="20" height="112" rx="6"/>
  <rect class="wheel" x="64" y="150" width="20" height="112" rx="6"/>
  <text class="bl" x="44" y="144" text-anchor="middle">BEND</text>
  <text class="bl" x="74" y="144" text-anchor="middle">MOD</text>
  <circle class="wheel" cx="114" cy="170" r="7"/><circle class="wheel" cx="142" cy="170" r="7"/>
  <text class="bl" x="128" y="156" text-anchor="middle">OCTAVE</text>
  {btn(*space, "SPACE", "space")}
  {keylabels}
  {"".join(keys)}
  {callout}
  {"".join(key_lines)}
  {marks}
</svg>'''


NOTES = ["Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"]


def key_name(k):
    """Where a key is on the 37-key keyboard: note and octave (1 = lowest; key 36 is the top C)."""
    return "Do aigu" if k == 36 else f"{NOTES[k % 12]} · octave {k // 12 + 1}"


def shortcuts_html():
    out = []
    for group in GROUPS:
        items = [x for x in SHORTCUTS if x[3] == group]
        lis = "".join(
            f'<li><button type="button" class="sc {kind}" data-k="{k}"><span class="sk-sw" aria-hidden="true"></span>'
            f'<span class="sc-label">{label}</span><span class="sc-key">{key_name(k)}</span></button></li>'
            for k, label, kind, _ in items)
        out.append(f'<div class="sc-group"><h3>{group}</h3><ul>{lis}</ul></div>')
    return "\n".join(out)


PANEL_PC = panel_svg("pc")
PANEL_KEYS = panel_svg("keys")
PANEL_CC = panel_svg("cc")
PANEL_DUMP = panel_svg("dump")

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
    "{{CABLING}}": CABLING, "{{PANEL_PC}}": PANEL_PC, "{{PANEL_CC}}": PANEL_CC, "{{PANEL_DUMP}}": PANEL_DUMP,
    "{{PANEL_KEYS}}": PANEL_KEYS, "{{SHORTCUTS}}": shortcuts_html(),
    "{{SHORTCUTS_JS}}": ",".join(f'"{k}":["{label}","{key_name(k)}","{kind}"]' for k, label, kind, _ in SHORTCUTS),
    "{{PAGE_BUTTONS}}": page_buttons, "{{PAGES_JS}}": pages_js,
    "{{FIRST_CAPTION}}": PAGES[0][2],
}.items():
    html = html.replace(k, v)
assert "{{" not in html, html[html.index("{{"):html.index("{{") + 40]
(HERE.parent.parent / "docs" / "index.html").write_text(html, encoding="utf-8")
print(len(html), "bytes")
