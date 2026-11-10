#!/usr/bin/env python3
"""The OXIde logo as SVG, drawn like the plug-in draws it (Source/ui/Atelier.cpp, drawLogo): the
centre lines of the five letters, each stroke made of three lines (a full stroke, its middle
cleared, its centre line drawn again), open ends cut flat at the cap height.

Used by tools/site/make_site.py (inlined in the page) and tools/dmg/make_background.py.
    python3 tools/logo/make_logo.py [out.svg] [colour]     writes the logo alone (default: oxide-logo.svg, #f3efe6)
Keep the numbers in step with Atelier.cpp."""
import sys
from pathlib import Path

STROKE, GAP = 0.2, 0.12                   # in cap heights
WIDTHS = [1.25, 1.1, 0.2, 1.05, 0.86]     # O X I D E
WIDTH = sum(WIDTHS) + GAP * (len(WIDTHS) - 1)


def _n(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def centre_lines():
    w, h, ext = STROKE, 1.0, 0.3
    d = []
    x = 0.0
    # O
    rx, ry = (WIDTHS[0] - w) / 2, (h - w) / 2
    cx, cy = x + WIDTHS[0] / 2, h / 2
    d.append(f"M{_n(cx - rx)} {_n(cy)}A{_n(rx)} {_n(ry)} 0 1 1 {_n(cx + rx)} {_n(cy)}A{_n(rx)} {_n(ry)} 0 1 1 {_n(cx - rx)} {_n(cy)}Z")
    x += WIDTHS[0] + GAP
    # X: two diagonals, running past the band
    a, b = x + w * 0.62, x + WIDTHS[1] - w * 0.62
    dx = (b - a) * ext
    d.append(f"M{_n(a - dx)} {_n(-ext)}L{_n(b + dx)} {_n(h + ext)}M{_n(b + dx)} {_n(-ext)}L{_n(a - dx)} {_n(h + ext)}")
    x += WIDTHS[1] + GAP
    # I
    d.append(f"M{_n(x + w / 2)} {_n(-ext)}L{_n(x + w / 2)} {_n(h + ext)}")
    x += WIDTHS[2] + GAP
    # D: straight back, round bowl
    r = (h - w) / 2
    cx = x + WIDTHS[3] - w / 2 - r
    d.append(f"M{_n(x + w / 2)} {_n(w / 2)}L{_n(cx)} {_n(w / 2)}A{_n(r)} {_n(r)} 0 0 1 {_n(cx)} {_n(h - w / 2)}L{_n(x + w / 2)} {_n(h - w / 2)}Z")
    x += WIDTHS[3] + GAP
    # E
    right = x + WIDTHS[4]
    d.append(f"M{_n(right)} {_n(w / 2)}L{_n(x + w / 2)} {_n(w / 2)}L{_n(x + w / 2)} {_n(h - w / 2)}L{_n(right)} {_n(h - w / 2)}"
             f"M{_n(x + w / 2)} {_n(h / 2)}L{_n(right - 0.06)} {_n(h / 2)}")
    return "".join(d)


def svg(ink="currentColor", ident="oxl", height=None, label="OXIde"):
    """The logo, one cap height = 1 unit. 'ident' keeps the ids unique when inlined twice."""
    path = centre_lines()
    size = f' height="{height}" width="{_n(height * WIDTH)}"' if height else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {_n(WIDTH)} 1"{size} role="img" aria-label="{label}">'
            f'<defs><clipPath id="{ident}-band"><rect width="{_n(WIDTH)}" height="1"/></clipPath>'
            f'<mask id="{ident}-ink" maskUnits="userSpaceOnUse" x="-1" y="-1" width="{_n(WIDTH + 2)}" height="3">'
            f'<g fill="none" stroke-linejoin="miter" stroke-linecap="butt">'
            f'<path d="{path}" stroke="#fff" stroke-width="{_n(STROKE)}"/>'
            f'<path d="{path}" stroke="#000" stroke-width="{_n(STROKE * 0.6)}"/>'
            f'<path d="{path}" stroke="#fff" stroke-width="{_n(STROKE * 0.2)}"/>'
            f'</g></mask></defs>'
            f'<g clip-path="url(#{ident}-band)"><rect x="-1" y="-1" width="{_n(WIDTH + 2)}" height="3" fill="{ink}" mask="url(#{ident}-ink)"/></g>'
            f'</svg>')


if __name__ == "__main__":
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("oxide-logo.svg")
    out.write_text(svg(sys.argv[2] if len(sys.argv) > 2 else "#f3efe6", height=100))
    print(out)
