"""The page's hands-on guide: everyday scenes with the latest features, step by step, each with a
small drawing. make_site.py puts it in the page ({{GUIDE}}).

Text markup: [SINE] is a label as the plugin prints it, {STORE} a button on the OSCar."""
import math
import re

# colours of the page (template.html, :root)
IV, MU, DK, RA, OR, CU, RD, AM, VBG = "#ece6d8", "#7b7973", "#131416", "#2d3035", "#ff9d4f", "#c86930", "#ff3d25", "#ffb347", "#170b04"


# ---------------------------------------------------------------- drawing helpers
def pt(cx, cy, r, deg):
    """A point on a knob's scale: 0 degrees at the top, clockwise."""
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


def arc(cx, cy, r, a0, a1):
    x0, y0 = pt(cx, cy, r, a0)
    x1, y1 = pt(cx, cy, r, a1)
    large = 1 if abs(a1 - a0) > 180 else 0
    sweep = 1 if a1 > a0 else 0
    return f"M{x0:.1f} {y0:.1f} A{r} {r} 0 {large} {sweep} {x1:.1f} {y1:.1f}"


def knob(cx, cy, r, pointer=0, lit=None, colour=OR, face=None, ticks=True):
    """A knob with its scale; lit = (from, to) degrees: the glowing arc of a modulation."""
    out = []
    if ticks:
        for d in range(-135, 136, 27):
            x0, y0 = pt(cx, cy, r + 4, d)
            x1, y1 = pt(cx, cy, r + 7, d)
            out.append(f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="{MU}" stroke-width="1.4"/>')
    if lit:
        p = arc(cx, cy, r + 5.5, *lit)
        out.append(f'<path d="{p}" fill="none" stroke="{colour}" stroke-opacity=".3" stroke-width="8" stroke-linecap="round"/>')
        out.append(f'<path d="{p}" fill="none" stroke="{colour}" stroke-width="3.2"/>')
        tx, ty = pt(cx, cy, r + 5.5, lit[1])
        out.append(f'<circle cx="{tx:.1f}" cy="{ty:.1f}" r="5" fill="{colour}" fill-opacity=".35"/><circle cx="{tx:.1f}" cy="{ty:.1f}" r="2.4" fill="#fff1d6"/>')
    out.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{RA}" stroke="{IV}" stroke-width="2"/>')
    if face:
        out.append(face(cx, cy, r))
    else:
        x, y = pt(cx, cy, r * 0.82, pointer)
        out.append(f'<line x1="{cx}" y1="{cy}" x2="{x:.1f}" y2="{y:.1f}" stroke="{IV}" stroke-width="2.6" stroke-linecap="round"/>')
    return "".join(out)


def bot(x, y, w, h, mood="happy"):
    """The little OSCar-ish box with two knob eyes."""
    ex, ey, er = w * 0.3, h * 0.36, min(w, h) * 0.13
    eyes = ""
    for cx in (x + ex, x + w - ex):
        if mood == "sleep":
            eyes += f'<path d="M{cx - er:.1f} {y + ey:.1f} q{er:.1f} {er:.1f} {2 * er:.1f} 0" fill="none" stroke="{IV}" stroke-width="2" stroke-linecap="round"/>'
        else:
            eyes += f'<circle cx="{cx:.1f}" cy="{y + ey:.1f}" r="{er:.1f}" fill="{DK}" stroke="{IV}" stroke-width="1.6"/>'
            dx = {"happy": 0, "worried": -er * 0.5, "wow": 0}[mood]
            eyes += f'<line x1="{cx:.1f}" y1="{y + ey:.1f}" x2="{cx + dx:.1f}" y2="{y + ey - er * 0.8:.1f}" stroke="{IV}" stroke-width="1.6" stroke-linecap="round"/>'
    mx, my = x + w / 2, y + h * 0.66
    mouth = {
        "happy": f'<path d="M{mx - 7:.1f} {my:.1f} q7 7 14 0" fill="none" stroke="{IV}" stroke-width="2" stroke-linecap="round"/>',
        "sleep": f'<path d="M{mx - 4:.1f} {my + 1:.1f} h8" stroke="{IV}" stroke-width="2" stroke-linecap="round"/>',
        "worried": f'<path d="M{mx - 6:.1f} {my + 3:.1f} q3 -4 6 0 t6 0" fill="none" stroke="{IV}" stroke-width="2" stroke-linecap="round"/>',
        "wow": f'<ellipse cx="{mx:.1f}" cy="{my + 1:.1f}" rx="3.2" ry="4" fill="{DK}" stroke="{IV}" stroke-width="1.6"/>',
    }[mood]
    led = f'<circle class="blink" cx="{x + w - 8:.1f}" cy="{y + 7:.1f}" r="2.6" fill="{RD}"/>'
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="7" fill="{RA}" stroke="{IV}" stroke-width="2"/>'
            f'<rect x="{x + 6}" y="{y + h - 7}" width="{w - 12}" height="3" rx="1.5" fill="{MU}"/>' + eyes + mouth + led)


def note(x, y, colour):
    return (f'<ellipse cx="{x}" cy="{y}" rx="5" ry="3.6" transform="rotate(-20 {x} {y})" fill="{colour}"/>'
            f'<path d="M{x + 4.4} {y - 1} V{y - 19} q6 3 8 9" fill="none" stroke="{colour}" stroke-width="2" stroke-linecap="round"/>')


def arrowhead(x, y, deg, colour, size=6):
    """A filled arrowhead pointing at deg (0: right, 90: down)."""
    a = math.radians(deg)
    p1 = (x - size * math.cos(a - 0.45), y - size * math.sin(a - 0.45))
    p2 = (x - size * math.cos(a + 0.45), y - size * math.sin(a + 0.45))
    return f'<path d="M{x:.1f} {y:.1f} L{p1[0]:.1f} {p1[1]:.1f} L{p2[0]:.1f} {p2[1]:.1f} Z" fill="{colour}"/>'


def svg(body, label=""):
    return f'<svg viewBox="0 0 160 112" role="presentation" aria-hidden="true" focusable="false">{body}</svg>'


# ---------------------------------------------------------------- the drawings
def art_nextday():
    moon = f'<path d="M30 14 A12 12 0 0 0 30 38 A14 14 0 0 1 30 14 Z" fill="#e9d9a6"/>'
    zz = (f'<g class="zz" fill="{MU}" font-weight="700"><text x="40" y="20" font-size="8">z</text>'
          f'<text x="46" y="13" font-size="10">z</text><text x="54" y="6" font-size="12">Z</text></g>')
    rays = "".join(
        f'<line x1="{pt(130, 26, 13, d)[0]:.1f}" y1="{pt(130, 26, 13, d)[1]:.1f}" x2="{pt(130, 26, 18, d)[0]:.1f}" y2="{pt(130, 26, 18, d)[1]:.1f}" stroke="{OR}" stroke-width="2" stroke-linecap="round"/>'
        for d in range(0, 360, 45))
    sun = f'<g class="spin">{rays}</g><circle cx="130" cy="26" r="9" fill="{OR}"/>'
    path = f'<path d="M50 34 Q82 14 110 30" fill="none" stroke="{MU}" stroke-width="1.6" stroke-dasharray="3 4"/>' + arrowhead(112, 31, 30, MU)
    return svg(moon + zz + sun + path + bot(52, 54, 56, 46, "happy") + note(122, 78, OR) + note(132, 64, IV))


def art_keep():
    bubble = (f'<path d="M16 8 H144 a6 6 0 0 1 6 6 V50 a6 6 0 0 1 -6 6 H118 l-6 10 l-4 -10 H16 a6 6 0 0 1 -6 -6 V14 a6 6 0 0 1 6 -6 Z" fill="{RA}" stroke="{IV}" stroke-width="2"/>'
              f'<rect x="22" y="17" width="70" height="4" rx="2" fill="{MU}"/><rect x="22" y="25" width="46" height="4" rx="2" fill="{MU}"/>')
    left = (f'<rect x="22" y="36" width="54" height="14" rx="3" fill="none" stroke="{MU}" stroke-width="1.6"/>'
            f'<path d="{arc(49, 43, 4.2, 40, 320)}" fill="none" stroke="{MU}" stroke-width="1.6"/>')
    heart = "M0 2.2 C0 -1 -4.6 -1.6 -4.6 1.6 C-4.6 4 0 6.4 0 7.6 C0 6.4 4.6 4 4.6 1.6 C4.6 -1.6 0 -1 0 2.2 Z"
    right = (f'<rect x="84" y="36" width="58" height="14" rx="3" fill="{OR}"/>'
             f'<path d="{heart}" transform="translate(113 39.4)" fill="{VBG}"/>')
    k = knob(40, 88, 13, pointer=60)
    love = f'<path class="beat" d="{heart}" transform="translate(58 66) scale(1.4)" fill="{RD}"/>'
    return svg(bubble + left + right + k + love + bot(92, 72, 46, 36, "happy"))


def art_modes():
    cx, cy = 80, 66
    out = [f'<path d="{arc(cx, cy, 34, -70, 70)}" fill="none" stroke="{MU}" stroke-width="1.4" stroke-dasharray="2 4"/>']
    # always (tick), ask (?), never (cross)
    x, y = pt(cx, cy, 44, -55)
    out.append(f'<path d="M{x - 6:.1f} {y:.1f} l4 5 l8 -10" fill="none" stroke="{IV}" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>')
    x, y = pt(cx, cy, 44, 0)
    out.append(f'<text x="{x:.1f}" y="{y + 6:.1f}" text-anchor="middle" font-size="18" font-weight="700" fill="{OR}">?</text>')
    out.append(f'<circle class="blink" cx="{x + 13:.1f}" cy="{y - 1:.1f}" r="3" fill="{RD}"/>')
    x, y = pt(cx, cy, 44, 55)
    out.append(f'<path d="M{x - 5:.1f} {y - 5:.1f} l10 10 M{x + 5:.1f} {y - 5:.1f} l-10 10" stroke="{IV}" stroke-width="2.4" stroke-linecap="round"/>')
    out.append(knob(cx, cy, 22, pointer=0, ticks=False))
    out.append(f'<circle cx="{cx}" cy="{cy}" r="15" fill="none" stroke="{MU}" stroke-width="1"/>')
    return svg("".join(out))


def art_channel():
    def walkie(x, digit, mood):
        return (f'<line x1="{x + 8}" y1="40" x2="{x + 8}" y2="18" stroke="{IV}" stroke-width="2.4" stroke-linecap="round"/>'
                f'<circle cx="{x + 8}" cy="17" r="2.6" fill="{IV}"/>'
                f'<rect x="{x}" y="38" width="38" height="62" rx="7" fill="{RA}" stroke="{IV}" stroke-width="2"/>'
                f'<rect x="{x + 6}" y="45" width="26" height="18" rx="2" fill="{VBG}"/>'
                f'<text class="mono" x="{x + 19}" y="59" text-anchor="middle" font-size="15" fill="{OR}">{digit}</text>'
                + "".join(f'<line x1="{x + 9}" y1="{y}" x2="{x + 29}" y2="{y}" stroke="{MU}" stroke-width="1.6" stroke-linecap="round"/>' for y in (84, 89, 94))
                + (f'<circle cx="{x + 13}" cy="70" r="1.8" fill="{IV}"/><circle cx="{x + 25}" cy="70" r="1.8" fill="{IV}"/>'
                   f'<path d="M{x + 14} 78 q5 {-4 if mood == "sad" else 4} 10 0" fill="none" stroke="{IV}" stroke-width="1.8" stroke-linecap="round"/>'))
    zig = f'<path d="M58 54 l8 -6 l6 8 l7 -7" fill="none" stroke="{OR}" stroke-width="2" stroke-linejoin="round"/><path d="M88 49 l7 7 l7 -6" fill="none" stroke="{OR}" stroke-width="2" stroke-linejoin="round" stroke-dasharray="2 3"/>'
    q = f'<text x="80" y="36" text-anchor="middle" font-size="20" font-weight="700" fill="{IV}">?</text>'
    return svg(walkie(14, "1", "sad") + walkie(108, "3", "sad") + zig + q)


def art_breathe():
    wave = "M4 62 " + " ".join(f"L{x} {62 - 20 * math.sin((x - 4) / 152 * 4 * math.pi):.1f}" for x in range(8, 157, 4))
    face = lambda cx, cy, r: (f'<path d="M{cx - 9} {cy - 3} q3 -3 6 0 M{cx + 3} {cy - 3} q3 -3 6 0" fill="none" stroke="{IV}" stroke-width="1.8" stroke-linecap="round"/>'
                              f'<ellipse cx="{cx}" cy="{cy + 8}" rx="3.5" ry="4.5" fill="{DK}" stroke="{IV}" stroke-width="1.6"/>')
    puff = lambda x, y, cls: (f'<g class="puff {cls}" fill="{IV}" fill-opacity=".85"><circle cx="{x}" cy="{y}" r="5"/>'
                              f'<circle cx="{x + 6}" cy="{y - 3}" r="6"/><circle cx="{x + 12}" cy="{y}" r="4.5"/></g>')
    return svg(f'<path d="{wave}" fill="none" stroke="{MU}" stroke-width="1.6" stroke-dasharray="3 3"/>'
               + knob(80, 62, 24, lit=(-30, 75), face=face) + puff(16, 30, "") + puff(124, 30, "late"))


def art_pump():
    handle = (f'<g class="pumpH"><line x1="45" y1="18" x2="45" y2="44" stroke="{IV}" stroke-width="3"/>'
              f'<line x1="30" y1="18" x2="60" y2="18" stroke="{IV}" stroke-width="5" stroke-linecap="round"/></g>')
    barrel = (f'<rect x="36" y="40" width="18" height="56" rx="3" fill="{CU}" stroke="{IV}" stroke-width="2"/>'
              f'<line x1="40" y1="48" x2="40" y2="88" stroke="#f4aa6c" stroke-width="2" stroke-linecap="round"/>'
              f'<path d="M26 100 H64" stroke="{IV}" stroke-width="4" stroke-linecap="round"/>')
    hose = f'<path d="M54 90 C80 108 92 96 102 84" fill="none" stroke="{MU}" stroke-width="3" stroke-linecap="round"/>'
    curve = (f'<path d="M86 46 C92 46 96 20 106 20 H112 V46 C118 46 122 20 132 20 H138 V46 C144 46 146 34 150 30" fill="none" '
             f'stroke="{OR}" stroke-width="2.2" stroke-linejoin="round"/>')
    return svg(curve + handle + barrel + hose + knob(118, 80, 13, lit=(-110, 40)))


def art_riff():
    die = (f'<g transform="rotate(-14 62 60)"><rect x="42" y="40" width="40" height="40" rx="8" fill="{IV}"/>'
           + "".join(f'<circle cx="{x}" cy="{y}" r="3.6" fill="{DK}"/>' for x, y in ((52, 50), (72, 50), (62, 60), (52, 70), (72, 70)))
           + "</g>")
    loop = (f'<path d="M42 87.7 A40 32 0 1 1 82 87.7" fill="none" stroke="{OR}" stroke-width="2.4" stroke-linecap="round" stroke-dasharray="6 5"/>'
            + arrowhead(81, 88.2, 155, OR, 8))
    return svg(loop + die + note(118, 44, OR) + note(136, 30, IV) + note(130, 70, OR))


def art_drift():
    line = "M4 88 " + " ".join(f"L{x} {88 - 7 * math.sin((x - 4) / 152 * 2.2 * math.pi):.1f}" for x in range(8, 157, 4))
    snail = (f'<g class="snail"><path d="M40 86 C40 76 52 74 74 74 L92 74 C100 74 104 70 106 64 L109 66 C108 76 102 86 90 86 Z" fill="#d8cfbd"/>'
             f'<line x1="102" y1="66" x2="106" y2="52" stroke="#d8cfbd" stroke-width="2"/><line x1="98" y1="68" x2="98" y2="54" stroke="#d8cfbd" stroke-width="2"/>'
             f'<circle cx="106" cy="51" r="2.4" fill="{IV}"/><circle cx="98" cy="53" r="2.4" fill="{IV}"/>'
             f'<circle cx="66" cy="62" r="17" fill="{CU}" stroke="{IV}" stroke-width="2"/>'
             f'<path d="M66 62 m0 -3 a3 3 0 1 1 -3 3 a6 6 0 1 1 6 6 a9 9 0 1 1 -9 -9 a12 12 0 1 1 12 12" fill="none" stroke="#f4aa6c" stroke-width="1.8" stroke-linecap="round"/></g>')
    vfd = (f'<rect x="104" y="12" width="48" height="20" rx="3" fill="{VBG}"/>'
           f'<text class="mono" x="128" y="27" text-anchor="middle" font-size="13" fill="{OR}">4.00 s</text>')
    return svg(f'<path d="{line}" fill="none" stroke="{OR}" stroke-width="2"/>' + snail + vfd)


def art_tidy():
    mouse = (f'<rect x="14" y="26" width="42" height="62" rx="21" fill="{RA}" stroke="{IV}" stroke-width="2"/>'
             f'<path d="M35 26 V50" stroke="{IV}" stroke-width="2"/>'
             f'<path d="M35 27 A20 20 0 0 1 55 47 V50 H35 Z" fill="{OR}"/>'
             f'<path d="M35 26 V50 M15 50 H55" stroke="{IV}" stroke-width="2" fill="none"/>'
             + "".join(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{OR}" stroke-width="2" stroke-linecap="round"/>'
                       for x1, y1, x2, y2 in ((60, 22, 66, 16), (62, 32, 70, 30), (52, 16, 54, 8))))
    k = knob(112, 42, 15, pointer=-40, lit=(-40, 70), colour=MU)
    off = f'<circle cx="{pt(112, 42, 25, 45)[0]:.1f}" cy="{pt(112, 42, 25, 45)[1]:.1f}" r="3" fill="#3a1310" stroke="{MU}" stroke-width="1"/>'
    trail = f'<path d="M78 98 Q100 86 122 98" fill="none" stroke="{OR}" stroke-width="3" stroke-linecap="round" stroke-dasharray="5 6" opacity=".7"/>'
    eraser = (f'<g class="rub"><g transform="rotate(-28 128 88)"><rect x="112" y="80" width="18" height="14" rx="2" fill="#e88f86"/>'
              f'<rect x="130" y="80" width="18" height="14" rx="2" fill="{IV}"/></g></g>')
    crumbs = "".join(f'<circle cx="{x}" cy="{y}" r="1.4" fill="#e88f86"/>' for x, y in ((104, 104), (110, 101), (98, 101)))
    return svg(mouse + k + off + trail + crumbs + eraser)


def art_scale():
    x0, w, top, bottom = 17, 18, 46, 100
    out = []
    for i in range(7):
        out.append(f'<rect x="{x0 + i * w}" y="{top}" width="{w}" height="{bottom - top}" rx="2" fill="{IV}" stroke="{DK}" stroke-width="1"/>')
    for i in (0, 1, 3, 4, 5):   # black keys after C, D, F, G, A
        out.append(f'<rect x="{x0 + (i + 1) * w - 6}" y="{top}" width="12" height="32" rx="1.5" fill="#111214"/>')
    for i in range(7):          # A minor: all the white keys
        out.append(f'<circle cx="{x0 + i * w + w / 2}" cy="{bottom - 9}" r="3.4" fill="{OR}"/>')
    gx = x0 + 5 * w             # G#, a wrong note, hops to A
    out.append(f'<circle cx="{gx}" cy="{top + 24}" r="3.4" fill="{RD}"/>')
    out.append(f'<path class="hop" d="M{gx} {top + 18} C{gx - 2} {top - 26} {gx + 22} {top - 26} {gx + 16} {bottom - 18}" fill="none" stroke="{OR}" stroke-width="2.2" stroke-linecap="round"/>')
    out.append(arrowhead(gx + 16, bottom - 16, 100, OR, 7))
    out.append(f'<path d="M{gx - 6} 22 l-6 -5 M{gx - 9} 30 l-7 -1 M{gx + 22} 21 l6 -5 M{gx + 25} 29 l7 -1" stroke="{MU}" stroke-width="1.6" stroke-linecap="round"/>')
    return svg("".join(out))


def art_morph():
    def star(cx, cy, ro, ri, n=6):
        pts = []
        for k in range(2 * n):
            r = ro if k % 2 == 0 else ri
            a = math.pi * k / n - math.pi / 2
            pts.append(f"{cx + r * math.cos(a):.1f},{cy + r * math.sin(a):.1f}")
        return " ".join(pts)
    a = f'<circle cx="30" cy="44" r="15" fill="{IV}"/>'
    mid = f'<polygon class="wobble" points="{star(80, 44, 17, 13)}" fill="#f6c69a" stroke="#f6c69a" stroke-width="4" stroke-linejoin="round"/>'
    b = f'<polygon points="{star(130, 44, 19, 8)}" fill="{OR}" stroke="{OR}" stroke-width="1.5" stroke-linejoin="round"/>'
    eyes = "".join(f'<circle cx="{x}" cy="42" r="1.8" fill="{DK}"/>' for x in (25, 35, 75, 85, 125, 135))
    labels = (f'<text x="30" y="76" text-anchor="middle" font-size="12" font-weight="700" fill="{MU}">A</text>'
              f'<text x="130" y="76" text-anchor="middle" font-size="12" font-weight="700" fill="{MU}">B</text>')
    fader = (f'<line x1="24" y1="94" x2="136" y2="94" stroke="#3a3d43" stroke-width="6" stroke-linecap="round"/>'
             + "".join(f'<line x1="{x}" y1="86" x2="{x}" y2="88" stroke="{MU}" stroke-width="1.4"/>' for x in range(24, 137, 14))
             + f'<g class="slide"><rect x="72" y="85" width="16" height="18" rx="2.5" fill="{IV}"/><line x1="73" y1="94" x2="87" y2="94" stroke="{DK}" stroke-width="1.6"/></g>')
    return svg(a + mid + b + eyes + labels + fader)


def art_draw():
    bars = []
    x0, step, base = 20, 12, 100
    for i in range(10):
        x = x0 + i * step
        y = 34 + 62 * (1 - 1 / (1 + 0.42 * i))   # under the stroke: a falling slope
        bars.append(f'<rect x="{x}" y="{y:.1f}" width="8" height="{base - y:.1f}" rx="1.5" fill="#3a3d43"/>'
                    f'<rect x="{x}" y="{y:.1f}" width="8" height="3.2" rx="1.2" fill="{OR}"/>')
    line = "M18 32 " + " ".join(f"L{x0 + i * step + 4} {34 + 62 * (1 - 1 / (1 + 0.42 * i)) - 2:.1f}" for i in range(10))
    pencil = (f'<g transform="translate(134 82) rotate(-35)">'
              f'<rect x="0" y="-4.5" width="30" height="9" fill="#f1d891"/><rect x="30" y="-4.5" width="6" height="9" fill="#e88f86"/>'
              f'<path d="M0 -4.5 L-9 0 L0 4.5 Z" fill="#e9d9a6"/><path d="M-6 -1.5 L-9 0 L-6 1.5 Z" fill="{DK}"/></g>')
    return svg("".join(bars) + f'<path d="{line}" fill="none" stroke="{OR}" stroke-width="2.4" stroke-linejoin="round" stroke-linecap="round" stroke-dasharray="1 0"/>' + pencil)


def art_store():
    # a drawer opening, a note dropping into it, and the three keys below
    cab = (f'<rect x="40" y="10" width="80" height="58" rx="5" fill="{RA}" stroke="{IV}" stroke-width="2"/>'
           f'<rect x="48" y="18" width="64" height="18" rx="2" fill="none" stroke="{MU}" stroke-width="1.6"/>'
           f'<rect x="74" y="25" width="12" height="4" rx="2" fill="{MU}"/>'
           f'<g class="drawer"><rect x="44" y="42" width="72" height="22" rx="2" fill="#3a3d43" stroke="{IV}" stroke-width="2"/>'
           f'<rect x="54" y="47" width="22" height="12" rx="2" fill="{VBG}"/>'
           f'<text class="mono" x="65" y="57" text-anchor="middle" font-size="10" fill="{OR}">7</text>'
           f'<rect x="88" y="51" width="16" height="4" rx="2" fill="{IV}"/></g>')
    drop = f'<g class="dropin">{note(128, 30, OR)}</g>'
    keys = ""
    for i, (x, w, label) in enumerate(((14, 44, "VOICE"), (64, 44, "STORE"), (114, 30, "7"))):
        keys += (f'<g class="press" style="animation-delay:{i * 0.35:.2f}s"><rect x="{x}" y="80" width="{w}" height="20" rx="3" fill="{IV}"/>'
                 f'<rect x="{x}" y="96" width="{w}" height="4" rx="2" fill="#bdb6a6"/>'
                 f'<text x="{x + w / 2}" y="93.5" text-anchor="middle" font-size="9" font-weight="700" fill="{DK}">{label}</text></g>')
    return svg(cab + drop + keys)


def art_disk():
    floppy = (f'<g transform="rotate(-8 64 56)"><path d="M30 18 H90 L102 30 V94 a4 4 0 0 1 -4 4 H34 a4 4 0 0 1 -4 -4 V22 a4 4 0 0 1 4 -4 Z" fill="{CU}" stroke="{IV}" stroke-width="2"/>'
              f'<rect x="44" y="18" width="40" height="24" rx="2" fill="#d8cfbd"/><rect x="72" y="22" width="8" height="16" rx="1" fill="{RA}"/>'
              f'<rect x="40" y="56" width="52" height="38" rx="3" fill="{IV}"/>'
              + "".join(f'<line x1="46" y1="{y}" x2="86" y2="{y}" stroke="#bdb6a6" stroke-width="2" stroke-linecap="round"/>' for y in (66, 74, 82))
              + f'<circle cx="52" cy="66" r="0" fill="none"/></g>')
    face = (f'<circle cx="58" cy="74" r="1.8" fill="{DK}"/><circle cx="72" cy="72" r="1.8" fill="{DK}"/>'
            f'<path d="M60 80 q6 5 12 -2" fill="none" stroke="{DK}" stroke-width="1.8" stroke-linecap="round"/>')
    sparkle = "".join(f'<path class="twinkle" style="animation-delay:{d}s" d="M{x} {y - 6} L{x + 1.6} {y - 1.6} L{x + 6} {y} L{x + 1.6} {y + 1.6} L{x} {y + 6} L{x - 1.6} {y + 1.6} L{x - 6} {y} L{x - 1.6} {y - 1.6} Z" fill="{AM}"/>'
                      for x, y, d in ((122, 24, 0), (134, 60, 0.6), (114, 88, 1.2)))
    return svg(floppy + face + sparkle)


ART = {"store": art_store, "disk": art_disk, "nextday": art_nextday, "keep": art_keep, "modes": art_modes, "channel": art_channel,
       "breathe": art_breathe, "pump": art_pump, "riff": art_riff, "drift": art_drift, "tidy": art_tidy,
       "scale": art_scale, "morph": art_morph, "draw": art_draw}


# ---------------------------------------------------------------- the scenes
# key: (title, situation or "", steps, "ol" or "ul", note or "")
SCENES = {
    "store": {
        "fr": ("Ranger mon son dans une mémoire de l'OSCar", "",
               ["Clique SAUVER, en bas de la fenêtre, onglet « Dans l'OSCar ».",
                "Choisis la mémoire : au départ, le même numéro que la voix.",
                "Clique « Ranger dans la mémoire » : l'OSCar joue la voix, SUPERMOD et MORPH s'arrêtent le temps de ranger.",
                "Sur l'OSCar, tape les touches affichées, {VOICE}, {STORE}, puis le numéro. Puis clique « C'est fait »."],
               "ol", "Seule cette mémoire change. Aucun message MIDI ne sait ranger une voix : d'où les touches à taper. « Toute la banque… » écrit les 36 voix d'un coup."),
        "en": ("Store my sound in one of the OSCar's memories", "",
               ["Click SAVE, at the bottom of the window, tab \"In the OSCar\".",
                "Pick the memory: at first, the same number as the voice.",
                "Click \"Store in this memory\": the OSCar plays the voice, SUPERMOD and MORPH hold while you store.",
                "On the OSCar, press the keys shown, {VOICE}, {STORE}, then the number. Then click \"Done\"."],
               "ol", "Only that memory changes. No MIDI message can store a voice: hence the keys to press. \"Whole bank...\" writes all 36 voices at once."),
    },
    "disk": {
        "fr": ("Garder une copie sur l'ordi", "SAUVER, onglet « Sur l'ordi » :",
               ["« Sauver voix… » : la voix choisie, dans Documents > OXIde > Voices ;",
                "« Enregistrer la banque… » : les 36 voix avec leurs noms, dans Documents > OXIde > Banks ;",
                "« Exporter en .syx… » : les 36 voix pour d'autres éditeurs, sans les noms ;",
                "« Ouvrir le dossier » : tout y est, avec les sauvegardes automatiques."],
               "ul", "Le projet Logic garde aussi sa banque : en le rouvrant, tu retrouves tes 36 voix."),
        "en": ("Keep a copy on the computer", "SAVE, tab \"On the computer\":",
               ["\"Save voice...\": the selected voice, in Documents > OXIde > Voices;",
                "\"Save bank as...\": the 36 voices with their names, in Documents > OXIde > Banks;",
                "\"Export .syx...\": the 36 voices for other editors, without the names;",
                "\"Open folder\": it's all there, with the automatic backups."],
               "ul", "The Logic project keeps its bank too: open it again and your 36 voices are back."),
    },
    "nextday": {
        "fr": ("Je rouvre mon morceau le lendemain",
               "Hier soir, le morceau était fini. Ce matin, quelqu'un a joué avec l'OSCar.",
               ["Ouvre le projet dans Logic, puis la fenêtre d'OXIde.",
                "Le plugin demande « Rappeler le son du projet ? » : réponds « Rappeler le son du projet ».",
                "L'OSCar rejoue le son exact du projet : toutes les valeurs, le tempo, et les formes d'onde utilisateur si la voix en joue une."],
               "ol", "Ses mémoires ne bougent pas : le son est joué, pas enregistré dans l'OSCar."),
        "en": ("I reopen my track the next day",
               "Last night the track was done. This morning, someone played with the OSCar.",
               ["Open the project in Logic, then OXIde's window.",
                "The plug-in asks \"Recall the project's sound?\": answer \"Recall the project's sound\".",
                "The OSCar plays the project's exact sound again: every value, the tempo, and the user waveforms if the voice plays one."],
               "ol", "Its memories stay as they were: the sound is played, not stored in the OSCar."),
    },
    "keep": {
        "fr": ("J'ouvre ma session en plein réglage sur l'OSCar",
               "Tu tiens un super son sur la machine, pas encore enregistré.",
               ["À la question « Rappeler le son du projet ? », réponds « Garder le son de l'OSCar » : rien n'est envoyé.",
                "Enregistre ton son sur l'OSCar avec {STORE}, dans une mémoire que tu peux écraser.",
                "Pour l'avoir dans le plugin : maintiens {INSERT}, appuie sur {VOICE} puis sur {SPACE}, et clique le numéro de cette mémoire."],
               "ol", "Tant que tu n'as pas répondu, le plugin n'envoie rien : ton son ne risque rien. Et chaque réception garde une sauvegarde des voix d'avant."),
        "en": ("I open my session while tweaking the OSCar",
               "You've got a great sound on the synth, not stored yet.",
               ["When asked \"Recall the project's sound?\", answer \"Keep the OSCar's sound\": nothing is sent.",
                "Store your sound on the OSCar with {STORE}, in a memory you can overwrite.",
                "To get it in the plug-in: hold {INSERT}, press {VOICE} then {SPACE}, and click that memory's number."],
               "ol", "Until you answer, the plug-in sends nothing: your sound is safe. And every receive keeps a backup of the voices before it."),
    },
    "modes": {
        "fr": ("Choisir une fois pour toutes", "Page Réglages, menu « Rappeler le son du projet à l'ouverture » :",
               ["<b>toujours</b> : l'OSCar retrouve le son du projet dès l'ouverture, sans question ;",
                "<b>demander d'abord</b>, le réglage de départ : le plugin attend ta réponse ;",
                "<b>jamais</b> : rien n'est envoyé à l'ouverture, pratique si l'OSCar sert surtout à la main ;",
                "« Rappeler maintenant », juste à côté, renvoie la voix choisie à l'OSCar quand tu veux."],
               "ul", "Le choix vaut pour tous tes projets."),
        "en": ("Decide once and for all", "Setup page, menu \"Recall the project's sound when it opens\":",
               ["<b>always</b>: the OSCar gets the project's sound as soon as it opens, no question asked;",
                "<b>ask first</b>, the default: the plug-in waits for your answer;",
                "<b>never</b>: nothing is sent when it opens, handy if you mostly play the OSCar by hand;",
                "\"Recall now\", right next to it, sends the selected voice to the OSCar whenever you like."],
               "ul", "The choice applies to all your projects."),
    },
    "channel": {
        "fr": ("L'OSCar ne réagit plus au plugin", "Les potards bougent dans OXIde, mais le son de l'OSCar ne change pas.",
               ["Joue une note sur l'OSCar. Si un bouton orange « Passer sur le canal N » apparaît en haut, à gauche de l'afficheur, l'OSCar et le plugin ne sont pas sur le même canal MIDI.",
                "Clique dessus : le plugin passe sur le canal de l'OSCar et tout répond de nouveau.",
                "Ou change le canal sur l'OSCar : maintiens {SPACE} et appuie sur une touche de 1 à 16. Aucun message MIDI ne peut le faire à sa place."],
               "ol", "Les CC Tauntek n'arrivent que sur le bon canal, même en mode omni. Il faut aussi le câble 2, de l'OSCar vers l'interface."),
        "en": ("The OSCar ignores the plug-in", "The knobs move in OXIde, but the OSCar's sound doesn't change.",
               ["Play a note on the OSCar. If an orange \"Use channel N\" button shows up at the top, left of the display, the OSCar and the plug-in aren't on the same MIDI channel.",
                "Click it: the plug-in moves to the OSCar's channel and everything responds again.",
                "Or change the channel on the OSCar: hold {SPACE} and press a key from 1 to 16. No MIDI message can do it instead."],
               "ol", "Tauntek CCs only get through on the right channel, even in omni mode. It also needs cable 2, from the OSCar to the interface."),
    },
    "breathe": {
        "fr": ("Un filtre qui respire en rythme", "",
               ["Allume Live CC et clique SUPERMOD, en bas de la fenêtre.",
                "Dans le bandeau : [SHAPE] [SINE], [SYNC] allumé, [RATE] sur [1/1].",
                "Sur la page Voix, tire le potard Frequency vers le haut jusqu'à [40].",
                "Lance la lecture dans Logic : le filtre s'ouvre et se ferme à chaque mesure, et l'arc lumineux autour du potard suit le mouvement."],
               "ol", "La voix elle-même ne change pas : le mouvement passe par-dessus, en CC. Clique SUPERMOD à nouveau pour retrouver les potards."),
        "en": ("A filter that breathes in time", "",
               ["Turn on Live CC and click SUPERMOD, at the bottom of the window.",
                "In the strip: [SHAPE] [SINE], [SYNC] on, [RATE] at [1/1].",
                "On the Voice page, drag the Frequency knob up to [40].",
                "Press play in Logic: the filter opens and closes every bar, and the lit arc around the knob follows the movement."],
               "ol", "The voice itself doesn't change: the movement rides on top, as CC. Click SUPERMOD again to get the knobs back."),
    },
    "pump": {
        "fr": ("Une basse qui pompe", "",
               ["Baisse un peu Frequency, puis clique SUPERMOD.",
                "Dans le bandeau : [SHAPE] [PUMP], [SYNC], [RATE] sur [1/4], [MODE] [UNIPOLAR].",
                "Tire Frequency vers le haut jusqu'à [60].",
                "À chaque temps, le filtre retombe sur la valeur du potard et remonte : l'effet sidechain, sans compresseur."],
               "ol", "Essaie aussi sur Filter Drive, réglé bas : c'est le volume qui pompe."),
        "en": ("A pumping bass", "",
               ["Turn Frequency down a little, then click SUPERMOD.",
                "In the strip: [SHAPE] [PUMP], [SYNC], [RATE] at [1/4], [MODE] [UNIPOLAR].",
                "Drag Frequency up to [60].",
                "On every beat, the filter drops back to the knob's value and swells up again: the sidechain effect, no compressor."],
               "ol", "Try it on Filter Drive too, set low: then the volume pumps."),
    },
    "drift": {
        "fr": ("Un pad qui vit tout seul", "",
               ["[SHAPE] [SMOOTH], [SYNC] éteint, [RATE] tourné vers [4.00 s] (à gauche, c'est plus lent), [LOOP] [OFF].",
                "Un peu de SUPERMOD sur plusieurs potards : PW [20], De Tune [10], Separation [-15], Osc Balance [25].",
                "Chaque potard glisse doucement vers une nouvelle valeur, chacun à sa façon : le son bouge sans jamais se répéter."],
               "ol", "Sync éteint, [RATE] va de 10 ms à 10 s, sans lien avec le tempo."),
        "en": ("A pad that lives on its own", "",
               ["[SHAPE] [SMOOTH], [SYNC] off, [RATE] turned to [4.00 s] (left is slower), [LOOP] [OFF].",
                "A little SUPERMOD on several knobs: PW [20], De Tune [10], Separation [-15], Osc Balance [25].",
                "Each knob glides slowly to a new value, each its own way: the sound moves and never repeats."],
               "ol", "With Sync off, [RATE] goes from 10 ms to 10 s, free of the tempo."),
    },
    "tidy": {
        "fr": ("Calmer, couper, effacer", "",
               ["Clic droit sur un potard, une tirette ou un sélecteur : « Couper sa modulation » (elle reste réglée, sa LED s'éteint), « Supprimer sa modulation » ou « Revenir à la valeur enregistrée ».",
                "[ACTIF], dans le bandeau SUPERMOD : éteint, tout s'arrête d'un coup, idéal pour un break ; rallumé, tout repart.",
                "[TOUT EFFACER] : plus aucune modulation.",
                "Double-clic sur un potard, calque fermé : il revient à la valeur enregistrée de la voix."],
               "ul", ""),
        "en": ("Tame, mute, erase", "",
               ["Right-click a knob, a fader or a switch: \"Switch its modulation off\" (it stays set, its LED goes dark), \"Remove its modulation\" or \"Back to the saved value\".",
                "[ACTIVE], in the SUPERMOD strip: off, everything stops at once, great for a break; back on, everything starts again.",
                "[CLEAR ALL]: no more modulations.",
                "Double-click a knob, layer closed: it goes back to the voice's saved value."],
               "ul", ""),
    },
    "scale": {
        "fr": ("Jouer juste sans y penser", "",
               ["Clique SCALE : choisis une gamme ([MAJOR], [DORIAN], [BLUES]…) et sa tonique sur le petit clavier.",
                "Allume MIDI Thru : les notes de ton clavier maître glissent vers la note de la gamme la plus proche. Même les fausses notes tombent juste.",
                "Transpose et Osc 2 Interval ne prennent plus que des notes de la gamme, leurs modulations aussi."],
               "ol", "Les notes jouées sur le clavier de l'OSCar lui-même ne passent pas par le plugin : elles restent telles quelles."),
        "en": ("Stay in key without thinking", "",
               ["Click SCALE: pick a scale ([MAJOR], [DORIAN], [BLUES]…) and its root on the little keyboard.",
                "Turn on MIDI Thru: the notes from your controller keyboard move to the nearest note of the scale. Even wrong notes land right.",
                "Transpose and Osc 2 Interval only take notes of the scale, their modulations too."],
               "ol", "Notes played on the OSCar's own keyboard don't go through the plug-in: they stay as they are."),
    },
    "riff": {
        "fr": ("Un petit riff aléatoire qui revient", "",
               ["Clique SCALE : [MINOR], et le La sur le petit clavier.",
                "Clique SUPERMOD : [SHAPE] [S&H], [SYNC], [RATE] sur [1/16], [LOOP] sur [16].",
                "Tire Transpose vers le haut jusqu'à [60], puis tiens une note (ou lance l'arpège).",
                "Les 16 pas reviennent à l'identique, toujours dans la gamme. « Nouvelle boucle » en tire un autre ; [LOOP] [OFF] change de note à chaque pas."],
               "ol", ""),
        "en": ("A little random riff that comes back", "",
               ["Click SCALE: [MINOR], and A on the little keyboard.",
                "Click SUPERMOD: [SHAPE] [S&H], [SYNC], [RATE] at [1/16], [LOOP] at [16].",
                "Drag Transpose up to [60], then hold a note (or start the arpeggiator).",
                "The 16 steps come back exactly the same, always in the scale. \"New loop\" rolls another one; [LOOP] [OFF] changes note on every step."],
               "ol", ""),
    },
    "morph": {
        "fr": ("Passer d'un son à l'autre en live", "",
               ["Page Morphing : choisis Voix A dans la liste de gauche, Voix B dans celle de droite.",
                "Allume MORPH : l'afficheur indique [MORPH EN COURS] et l'OSCar joue le mélange.",
                "Bouge le fader, ou automatise-le dans Logic (paramètre Morph A-B). Aucune voix n'est modifiée.",
                "Un mélange te plaît ? « Garder le mélange » l'écrit dans la voix choisie (Annuler le reprend). Éteins MORPH pour revenir à la voix."],
               "ol", ""),
        "en": ("Go from one sound to another, live", "",
               ["Morph page: pick Voice A in the left list, Voice B in the right one.",
                "Turn MORPH on: the display shows [MORPH ON] and the OSCar plays the blend.",
                "Move the fader, or automate it in Logic (parameter Morph A-B). No voice is changed.",
                "Like a blend? \"Keep the blend\" writes it into the selected voice (Undo takes it back). Turn MORPH off to go back to the voice."],
               "ol", ""),
    },
    "draw": {
        "fr": ("Dessiner une forme d'onde d'un seul geste", "",
               ["Page Formes d'onde : reçois d'abord celles de l'OSCar ({INSERT}, un bouton {WAVEFORM}, puis {SPACE}), ou clique « Partir de sinus ».",
                "Choisis une forme, de −8 à −12, et trace un trait à travers les barres : chaque barre traversée prend la hauteur du trait, même en allant vite.",
                "Une pente qui descend donne un son brillant, proche de la dent de scie ; un zigzag, une barre sur deux, un son plus creux, façon carré.",
                "Du rouge dans « Ce que joue l'OSCar » ? Ça sature : « Ajuster le niveau ». Puis « Envoyer les formes d'onde »."],
               "ol", "Live CC allumé, l'OSCar joue tout de suite la nouvelle forme si la voix l'utilise. Le tuto illustré est <a href=\"#tuto-formes\">juste en dessous</a>."),
        "en": ("Draw a waveform in one stroke", "",
               ["Waveforms page: first receive the OSCar's ({INSERT}, a {WAVEFORM} button, then {SPACE}), or click \"Start from sines\".",
                "Pick a waveform, from −8 to −12, and draw a line across the bars: every bar you cross takes the line's height, even when you go fast.",
                "A falling slope gives a bright sound, close to a sawtooth; a zigzag, every other bar, a hollower sound, square-like.",
                "Red in \"What the OSCar plays\"? It saturates: \"Fit level\". Then \"Send waveforms\"."],
               "ol", "With Live CC on, the OSCar plays the new waveform right away if the voice uses it. The illustrated tutorial is <a href=\"#tuto-formes\">just below</a>."),
    },
}

BANDS = [
    ({"fr": "le projet et l'oscar", "en": "the project and the oscar"}, ["nextday", "keep", "modes", "channel"]),
    ({"fr": "sauver", "en": "save"}, ["store", "disk"]),
    ({"fr": "supermod", "en": "supermod"}, ["breathe", "pump", "drift", "tidy"]),
    ({"fr": "scale, morph et formes d'onde", "en": "scale, morph and waveforms"}, ["scale", "riff", "morph", "draw"]),
]

INTRO = {
    "fr": ("Les nouveautés en situation",
           "Des scènes de tous les jours avec OXIde, pas à pas. Pour entendre ce que tu règles, Live CC doit être allumé, en bas de la fenêtre."),
    "en": ("The new features in practice",
           "Everyday scenes with OXIde, step by step. To hear what you set, Live CC must be on, at the bottom of the window."),
}


def markup(text):
    text = text.replace("&", "&amp;").replace("&amp;amp;", "&amp;")
    text = re.sub(r"\[([^\]]+)\]", r'<span class="ui">\1</span>', text)
    return re.sub(r"\{([A-Z0-9/ ]+)\}", r"<kbd>\1</kbd>", text)


def scene_html(key, lang):
    title, situation, steps, kind, note_text = SCENES[key][lang]
    items = "".join(f"<li>{markup(s)}</li>" for s in steps)
    out = [f'<article class="scene"><div class="scene-art">{ART[key]()}</div><div class="scene-text">',
           f"<h3>{markup(title)}</h3>"]
    if situation:
        out.append(f'<p class="situation">{markup(situation)}</p>')
    out.append(f'<{kind} class="how">{items}</{kind}>')
    if note_text:
        out.append(f'<p class="small">{markup(note_text)}</p>')
    out.append("</div></article>")
    return "".join(out)


# ---------------------------------------------------------------- the waveforms tutorial
def art_send():
    # the computer sends its waveform to the little OSCar, which is pleased
    screen = (f'<rect x="8" y="22" width="56" height="40" rx="4" fill="{RA}" stroke="{IV}" stroke-width="2"/>'
              f'<rect x="14" y="28" width="44" height="28" rx="2" fill="{VBG}"/>'
              f'<path d="M17 44 L22 34 L27 46 L32 36 L37 44 L42 38 L47 42 L54 40" fill="none" stroke="{OR}" stroke-width="1.8" stroke-linejoin="round"/>'
              f'<path d="M2 70 H70 L64 62 H8 Z" fill="{RA}" stroke="{IV}" stroke-width="2" stroke-linejoin="round"/>')
    bits = "".join(f'<rect class="bit" style="animation-delay:{d:.2f}s" x="70" y="{40 + (i % 2) * 6}" width="7" height="5" rx="1.5" fill="{OR}"/>'
                   for i, d in enumerate((0, 0.3, 0.6, 0.9)))
    return svg(screen + bits + bot(100, 26, 52, 44, "wow") + note(140, 92, OR))


def art_listen():
    wave = "M18 92 " + " ".join(f"L{x} {92 - 10 * math.sin((x - 18) / 34 * 2 * math.pi) - 4 * math.sin((x - 18) / 17 * 2 * math.pi):.1f}" for x in range(20, 143, 3))
    return svg(bot(52, 16, 56, 46, "happy")
               + f'<path class="sing" d="{wave}" fill="none" stroke="{OR}" stroke-width="2.4" stroke-linecap="round"/>'
               + note(126, 30, OR) + note(30, 34, IV))


TUTO = {
    "fr": {
        "legend": "tuto : formes d'onde",
        "lead": "Dessiner tes propres formes d'onde pour l'OSCar, en cinq étapes.",
        "steps": [
            ("Recevoir les formes de l'OSCar",
             "Sur l'OSCar, maintiens {INSERT}, appuie sur un bouton {WAVEFORM}, puis {SPACE} : il envoie ses cinq formes utilisateur, qui s'affichent dans la page Formes d'onde. Pas d'OSCar sous la main ? « Partir de sinus » donne cinq sinus à modifier ; les envoyer remplacera celles de l'OSCar.",
             ("img", "tuto-recevoir.webp", 1000, 408, "La page Formes d'onde avant d'avoir reçu les formes de l'OSCar")),
            ("Choisir une forme et dessiner",
             "Clique une des formes, de [ONDE -12] à [ONDE -8], puis trace un trait à travers les barres : chaque harmonique traversée prend la hauteur du trait, même en allant vite. Les barres vont de 0 à 127 ; 127, c'est une harmonique seule à plein niveau.",
             ("img", "tuto-dessin.webp", 1000, 604, "Une forme dessinée : une pente qui descend")),
            ("Regarder ce que joue l'OSCar",
             "L'écran de droite montre la forme telle que l'OSCar la calcule. En rouge, elle sature : la somme des harmoniques dépasse ce qu'il peut jouer, et le haut de l'onde est coupé. Clique [AJUSTER LE NIVEAU] : tout est remis à l'échelle, comme l'OSCar le fait avec ses propres formes.",
             ("pair", ("tuto-sature.webp", "Sature"), ("tuto-ajuste.webp", "Ajustée"), 500, 726)),
            ("Envoyer",
             "Clique « Envoyer les formes d'onde », en haut à droite, et confirme. Si une forme sature encore, le plugin propose « Ajuster et envoyer ». Les cinq formes −8 à −12 de l'OSCar sont remplacées ; une copie part dans les sauvegardes.",
             ("art", "send")),
            ("L'écouter",
             "Le plus simple : sous l'écran de droite, clique [OSC 1], [OSC 2] ou [OSC 1 + 2], Live CC allumé. Le plugin met Wave 1, Wave 2 ou les deux de la voix sur ta forme et l'OSCar la joue ; le bouton allumé dit quel oscillateur la joue. À la main : mets Wave 1 ou Wave 2 sur −8 à −12, page Voix. Live CC allumé, le plugin passe un instant l'oscillateur sur une autre forme et le remet, et l'OSCar joue la nouvelle. Sans Live CC, fais-le à la main sur l'OSCar : une autre forme, puis la tienne (il ne reconstruit une forme que quand on change de numéro).",
             ("art", "listen")),
        ],
        "box_title": "Sur la machine, sans le plugin",
        "box": ["{OSC1} + {0} : l'oscillateur 1 joue la forme en cours d'écriture.",
                "{OSC1} (INS) puis une touche de 1 à 24 : ajoute de cette harmonique ; {OSC2} (DEL) puis la touche : en retire.",
                "Pour sortir : {OSC1} + une autre touche, qui choisit une autre forme."],
        "box_note": "D'après les essais de Vincent.",
    },
    "en": {
        "legend": "tutorial: waveforms",
        "lead": "Draw your own waveforms for the OSCar, in five steps.",
        "steps": [
            ("Receive the OSCar's waveforms",
             "On the OSCar, hold {INSERT}, press a {WAVEFORM} button, then {SPACE}: it sends its five user waveforms, which show up on the Waveforms page. No OSCar at hand? \"Start from sines\" gives five sines to change; sending them will replace the OSCar's.",
             ("img", "tuto-recevoir.webp", 1000, 408, "The Waveforms page before the OSCar's waveforms are received")),
            ("Pick a waveform and draw",
             "Click one of the waveforms, [WAVE -12] to [WAVE -8], then draw a line across the bars: every harmonic you cross takes the line's height, even when you go fast. The bars go from 0 to 127; 127 is a harmonic alone at full level.",
             ("img", "tuto-dessin.webp", 1000, 604, "A waveform drawn: a falling slope")),
            ("Look at what the OSCar plays",
             "The screen on the right shows the waveform as the OSCar computes it. Red means it saturates: the sum of the harmonics is more than it can play, and the top of the wave is cut off. Click [FIT LEVEL]: everything is scaled back, as the OSCar does with its own waveforms.",
             ("pair", ("tuto-sature.webp", "Saturates"), ("tuto-ajuste.webp", "Fitted"), 500, 726)),
            ("Send",
             "Click \"Send waveforms\", top right, and confirm. If a waveform still saturates, the plug-in offers \"Fit and send\". The OSCar's five waveforms −8 to −12 are replaced; a copy goes to the backups.",
             ("art", "send")),
            ("Listen",
             "Simplest: under the screen on the right, click [OSC 1], [OSC 2] or [OSC 1 + 2], with Live CC on. The plug-in sets Wave 1, Wave 2 or both of the voice to your waveform and the OSCar plays it; the lit button tells which oscillator plays it. By hand: set Wave 1 or Wave 2 to −8 to −12 on the Voice page. With Live CC on, the plug-in switches the oscillator to another waveform for a moment and back, and the OSCar plays the new one. Without Live CC, do it by hand on the OSCar: another waveform, then yours (it only rebuilds a waveform when the number changes).",
             ("art", "listen")),
        ],
        "box_title": "On the synth, without the plug-in",
        "box": ["{OSC1} + {0}: oscillator 1 plays the waveform being written.",
                "{OSC1} (INS) then a key from 1 to 24: adds some of that harmonic; {OSC2} (DEL) then the key: takes some away.",
                "To leave: {OSC1} + another key, which picks another waveform."],
        "box_note": "From Vincent's tests.",
    },
}


# ---------------------------------------------------------------- how the OSCar keeps its waveforms, as a kitchen
def chef(x, y, w, h, mood="happy"):
    """The little OSCar box with a chef's hat."""
    cx = x + w / 2
    hat = (f'<path d="M{cx - 15:.1f} {y + 1} v-9 a7 7 0 0 1 4 -13 a9 9 0 0 1 22 0 a7 7 0 0 1 4 13 v9 Z" fill="{IV}"/>'
           f'<rect x="{cx - 15:.1f}" y="{y - 2}" width="30" height="5" rx="1.5" fill="#d8cfbd"/>')
    return bot(x, y, w, h, mood) + hat


def card(x, y, label, colour=IV, rot=0, fresh=False):
    """A recipe card with its waveform number and a little wave drawn on it."""
    wave = f'M{x + 5} {y + 22} q4 -9 8 0 t8 0 t8 0' if not fresh else f'M{x + 5} {y + 22} l4 -8 l4 12 l4 -10 l4 8 l4 -5 l4 3'
    return (f'<g transform="rotate({rot} {x + 18} {y + 16})"><rect x="{x}" y="{y}" width="36" height="30" rx="3" fill="{colour}"/>'
            f'<text class="mono" x="{x + 3}" y="{y + 10}" font-size="9" fill="{DK}">{label}</text>'
            f'<path d="{wave}" fill="none" stroke="{OR if fresh else CU}" stroke-width="1.6" stroke-linecap="round"/></g>')


def art_box():
    # the recipe box: five cards kept, and the draft "0" on the side, with a coffee stain
    box = (f'<path d="M14 54 H112 V100 H14 Z" fill="{CU}" stroke="{IV}" stroke-width="2"/>'
           f'<path d="M14 54 L24 44 H122 L112 54" fill="#a8562a" stroke="{IV}" stroke-width="2" stroke-linejoin="round"/>'
           f'<path d="M112 54 L122 44 V90 L112 100" fill="#a8562a" stroke="{IV}" stroke-width="2" stroke-linejoin="round"/>')
    cards = "".join(card(12 + i * 20, 8 + (i % 2) * 4, f"-{12 - i}", IV, -6 + i * 3) for i in range(5))
    draft = (f'<g transform="rotate(10 138 30)"><rect x="124" y="12" width="30" height="36" rx="2" fill="#f4eedc"/>'
             f'<text class="mono" x="139" y="26" text-anchor="middle" font-size="11" fill="{DK}">0</text>'
             f'<path d="M128 38 q3 -5 6 0 t6 0 t6 0" fill="none" stroke="{MU}" stroke-width="1.4" stroke-dasharray="2 2"/></g>'
             f'<circle cx="146" cy="56" r="7" fill="none" stroke="#8a5a32" stroke-width="2" opacity=".7"/>')
    return svg(cards + box + draft)


def art_cook():
    # the oscillator reads its card once and bakes the wave; then it plays that one
    return svg(card(8, 40, "-8", IV, -8)
               + f'<path d="M48 56 h14" stroke="{MU}" stroke-width="2" stroke-dasharray="3 3"/>' + arrowhead(66, 56, 0, MU)
               + chef(70, 34, 44, 38)
               + f'<path d="M120 80 h32 v10 a4 4 0 0 1 -4 4 h-24 a4 4 0 0 1 -4 -4 Z" fill="#3a3d43"/>'
               + f'<path class="sing" d="M122 80 q4 -12 8 0 t8 0 t8 0" fill="none" stroke="{OR}" stroke-width="2.4" stroke-linecap="round"/>'
               + note(136, 30, OR))


def art_post():
    # OXIde, the postman, swaps the five cards at once; the cook still serves the old cake
    env = (f'<g class="bob"><rect x="6" y="22" width="52" height="34" rx="3" fill="{IV}"/>'
           f'<path d="M6 24 L32 42 L58 24" fill="none" stroke="#bdb6a6" stroke-width="2"/>'
           f'<text class="mono" x="32" y="66" text-anchor="middle" font-size="9" fill="{OR}">x5</text></g>')
    box = (f'<rect x="64" y="40" width="40" height="30" rx="3" fill="{CU}" stroke="{IV}" stroke-width="2"/>'
           + "".join(f'<rect x="{68 + i * 6}" y="{32 + (i % 2) * 2}" width="8" height="12" rx="1" fill="#f4eedc"/>' for i in range(5)))
    bubble = (f'<path d="M110 14 H154 a4 4 0 0 1 4 4 V40 a4 4 0 0 1 -4 4 H130 l-6 8 l-1 -8 H110 a4 4 0 0 1 -4 -4 V18 a4 4 0 0 1 4 -4 Z" fill="{RA}" stroke="{IV}" stroke-width="1.6"/>'
              f'<path d="M116 34 q4 -10 8 0 t8 0 t8 0" fill="none" stroke="{CU}" stroke-width="2"/>'
              f'<text x="148" y="30" font-size="12" font-weight="700" fill="{IV}">?</text>')
    return svg(env + f'<path d="M58 50 h4" stroke="{MU}" stroke-width="2"/>' + box + bubble + chef(112, 66, 40, 34, "worried"))


def art_reorder():
    # another number for a moment, then yours: the cook bakes the new card
    tickets = (f'<g transform="rotate(-8 30 34)"><rect x="10" y="20" width="40" height="26" rx="2" fill="#f4eedc"/>'
               f'<path d="M16 38 l8 -12 l8 12 Z" fill="none" stroke="{DK}" stroke-width="1.6" stroke-linejoin="round"/>'
               f'<path d="M14 30 l26 12 M40 30 l-26 12" stroke="{RD}" stroke-width="2" opacity=".8"/></g>'
               f'<g transform="rotate(5 30 76)"><rect x="10" y="62" width="40" height="26" rx="2" fill="#f4eedc"/>'
               f'<text class="mono" x="30" y="80" text-anchor="middle" font-size="12" fill="{DK}">-8</text></g>')
    arrows = (f'<path d="M54 34 C70 34 70 52 82 54" fill="none" stroke="{MU}" stroke-width="2" stroke-dasharray="3 3"/>'
              f'<path d="M54 76 C70 76 70 60 82 58" fill="none" stroke="{OR}" stroke-width="2"/>' + arrowhead(84, 57, -10, OR))
    cake = (f'<path d="M124 84 h30 v10 a4 4 0 0 1 -4 4 h-22 a4 4 0 0 1 -4 -4 Z" fill="#3a3d43"/>'
            f'<path d="M126 84 l4 -10 l4 14 l4 -12 l4 9 l4 -6 l4 4" fill="none" stroke="{OR}" stroke-width="2.2" stroke-linejoin="round"/>')
    return svg(tickets + arrows + chef(88, 34, 40, 34, "wow") + cake)


def art_mold():
    # a cake too big overflows the mould (saturates); fitted, it holds
    mould = lambda x: (f'<path d="M{x} 50 h56 l-6 40 h-44 Z" fill="#3a3d43" stroke="{IV}" stroke-width="2" stroke-linejoin="round"/>')
    over = (f'<path d="M14 52 C14 26 30 18 42 30 C54 16 72 26 70 52" fill="#f6c69a"/>'
            f'<path d="M12 50 q-6 8 -2 16 M72 50 q6 8 2 18" stroke="#f6c69a" stroke-width="6" stroke-linecap="round" fill="none"/>'
            f'<path d="M12 50 h60" stroke="{RD}" stroke-width="3"/>')
    fit = (f'<path d="M94 52 C94 40 106 34 116 40 C126 34 140 40 140 52 Z" fill="#f6c69a"/>')
    return svg(mould(14) + over + mould(88) + fit
               + f'<text x="42" y="106" text-anchor="middle" font-size="10" font-weight="700" fill="{RD}">!</text>'
               + f'<path d="M108 102 l4 4 l8 -8" fill="none" stroke="{OR}" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>')


KITCHEN = {
    "fr": {
        "title": "D'abord, comment l'OSCar range ses formes (version cuisine)",
        "cards": [
            ("box", "La boîte à recettes",
             "L'OSCar a <b>cinq fiches recettes</b> gardées dans une boîte : les formes −12 à −8. Elles restent même éteint. Il a aussi un <b>brouillon</b>, la forme « 0 », griffonné à côté avec ses touches OSC1 et OSC2 : un brouillon, pas une fiche."),
            ("cook", "Le cuistot",
             "Chaque oscillateur est un <b>cuistot</b>. Quand on lui demande une forme, il lit la fiche <b>une seule fois</b>, fait cuire son gâteau (il calcule l'onde), puis sert ce gâteau-là tant qu'on ne lui demande rien d'autre."),
            ("post", "Le facteur OXIde",
             "« Envoyer les formes d'onde », c'est OXIde en <b>facteur</b> : il remplace les cinq fiches de la boîte d'un coup (il ne sait pas en livrer une seule). Mais le cuistot ne regarde pas la boîte : il continue de servir l'<b>ancien gâteau</b>."),
            ("reorder", "Recommander",
             "Pour qu'il recuise, il faut lui <b>commander autre chose une seconde, puis ta forme</b> : il relit la fiche, la nouvelle. OXIde le fait tout seul (Triangle, puis ta forme)… à condition de savoir quelle forme il cuisine. Si tu l'as commandée <b>directement à l'OSCar</b>, OXIde ne l'a pas vu : clique [ÉCOUTER SUR L'OSCAR], c'est OXIde qui passe la commande."),
            ("mold", "Le moule",
             "Le gâteau doit tenir dans un <b>moule</b> : si la somme des harmoniques est trop grosse, ça déborde et l'onde est coupée (le rouge dans « Ce que joue l'OSCar »). [AJUSTER LE NIVEAU] réduit la recette pour qu'elle tienne, comme l'OSCar le fait avec ses propres fiches."),
        ],
    },
    "en": {
        "title": "First, how the OSCar keeps its waveforms (kitchen version)",
        "cards": [
            ("box", "The recipe box",
             "The OSCar has <b>five recipe cards</b> kept in a box: waveforms −12 to −8. They stay there even when it's off. It also has a <b>draft</b>, waveform \"0\", scribbled next to it with its OSC1 and OSC2 keys: a draft, not a card."),
            ("cook", "The cook",
             "Each oscillator is a <b>cook</b>. Asked for a waveform, it reads the card <b>once</b>, bakes its cake (computes the wave), then serves that cake until it's asked for something else."),
            ("post", "OXIde the postman",
             "\"Send waveforms\" is OXIde as a <b>postman</b>: it swaps the five cards in the box at once (it can't deliver just one). But the cook doesn't look in the box: it keeps serving the <b>old cake</b>."),
            ("reorder", "Order again",
             "To make it bake again, <b>order something else for a second, then your waveform</b>: it reads the card again, the new one. OXIde does it for you (Triangle, then yours)… as long as it knows what the cook is baking. If you ordered it <b>on the OSCar itself</b>, OXIde didn't see: click [LISTEN ON THE OSCAR], and OXIde places the order."),
            ("mold", "The mould",
             "The cake has to fit in a <b>mould</b>: if the harmonics add up to too much, it overflows and the wave gets cut (the red in \"What the OSCar plays\"). [FIT LEVEL] scales the recipe down so it fits, as the OSCar does with its own cards."),
        ],
    },
}
KITCHEN_ART = {"box": art_box, "cook": art_cook, "post": art_post, "reorder": art_reorder, "mold": art_mold}


def kitchen_html(lang):
    k = KITCHEN[lang]
    cards = "".join(f'<article class="scene kitchen-card"><div class="scene-art">{KITCHEN_ART[key]()}</div><div class="scene-text">'
                    f'<h3>{markup(title)}</h3><p>{markup(text)}</p></div></article>'
                    for key, title, text in k["cards"])
    return f'<div class="kitchen"><h4 class="kitchen-title">{k["title"]}</h4><div class="scenes kitchen-grid">{cards}</div></div>'


def tuto_html(lang):
    t = TUTO[lang]
    img = "img/" if lang == "fr" else "img/en/"
    steps = []
    for i, (title, text, fig) in enumerate(t["steps"], 1):
        if fig[0] == "img":
            _, src, w, h, alt = fig
            figure = f'<img class="tuto-shot" src="{img}{src}" width="{w}" height="{h}" alt="{alt}" loading="lazy">'
        elif fig[0] == "pair":
            _, (a, ca), (b, cb), w, h = fig
            figure = ('<div class="tuto-pair">'
                      f'<figure><img class="tuto-shot" src="{img}{a}" width="{w}" height="{h}" alt="{ca}" loading="lazy"><figcaption>{ca}</figcaption></figure>'
                      f'<figure><img class="tuto-shot" src="{img}{b}" width="{w}" height="{h}" alt="{cb}" loading="lazy"><figcaption>{cb}</figcaption></figure></div>')
        else:
            figure = f'<div class="scene-art tuto-art">{ {"send": art_send, "listen": art_listen}[fig[1]]() }</div>'
        steps.append(f'<li class="tuto-step"><div class="tuto-text"><span class="tuto-num">{i}</span><h4>{markup(title)}</h4>'
                     f'<p>{markup(text)}</p></div><div class="tuto-fig">{figure}</div></li>')
    box = "".join(f"<li>{markup(x)}</li>" for x in t["box"])
    return (f'<div class="guide-band tuto" id="tuto-formes"><h3 class="mini-legend">{t["legend"]}</h3>'
            f'<p class="tuto-lead">{t["lead"]}</p>{kitchen_html(lang)}<ol class="tuto-steps">{"".join(steps)}</ol>'
            f'<aside class="tuto-box"><h4>{t["box_title"]}</h4><ul class="how">{box}</ul><p class="small">{t["box_note"]}</p></aside></div>')


def guide_html(lang):
    title, lead = INTRO[lang]
    bands = "".join(
        f'<div class="guide-band"><h3 class="mini-legend">{name[lang]}</h3><div class="scenes">'
        + "".join(scene_html(k, lang) for k in keys) + "</div></div>"
        for name, keys in BANDS)
    return (f'<p class="legend">guide</p>\n  <h2 id="t-guide">{title}</h2>\n  <p class="lead-text">{lead}</p>\n  {bands}\n  {tuto_html(lang)}')
