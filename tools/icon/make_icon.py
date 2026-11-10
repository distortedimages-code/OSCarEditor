#!/usr/bin/env python3
"""The OXIde app icon ("Pad lumineux"): a glowing copper pad in a dark bezel, the triple-stroke O of
the logo engraved on it, on the graphite plate of macOS's rounded square (1024 canvas, 824 body).

Writes Resources/Icon/OXIde-icon.svg and renders, with rsvg-convert (librsvg):
  Resources/Icon/OXIde-1024.png   the app icon (CMake's ICON_BIG: JUCE makes the .icns from it)
  docs/img/icon-64.png            the web page's favicon
  docs/img/icon-180.png           its touch icon
    python3 tools/icon/make_icon.py"""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

SVG = """<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs>
    <filter id="sh" x="-10%" y="-10%" width="120%" height="125%"><feDropShadow dx="0" dy="14" stdDeviation="14" flood-color="#000000" flood-opacity="0.5"/></filter>
    <linearGradient id="pl" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#35383d"/><stop offset="1" stop-color="#151619"/></linearGradient>
    <pattern id="br" width="6" height="6" patternUnits="userSpaceOnUse"><rect width="2" height="6" fill="#ffffff" fill-opacity="0.03"/></pattern>
    <filter id="halo" x="-40%" y="-40%" width="180%" height="180%"><feGaussianBlur stdDeviation="30"/></filter>
    <radialGradient id="face" cx="0.5" cy="0.45" r="0.72"><stop offset="0" stop-color="#f7a36e"/><stop offset="0.4" stop-color="#e0703a"/><stop offset="0.8" stop-color="#a8461d"/><stop offset="1" stop-color="#6a2b10"/></radialGradient>
    <mask id="ink" maskUnits="userSpaceOnUse" x="0" y="0" width="1024" height="1024">
      <g fill="none">
        <circle cx="512" cy="512" r="150" stroke="#ffffff" stroke-width="60"/>
        <circle cx="512" cy="512" r="150" stroke="#000000" stroke-width="36"/>
        <circle cx="512" cy="512" r="150" stroke="#ffffff" stroke-width="12"/>
      </g>
    </mask>
  </defs>
  <rect x="100" y="100" width="824" height="824" rx="184" fill="url(#pl)" filter="url(#sh)"/>
  <rect x="100" y="100" width="824" height="824" rx="184" fill="url(#br)"/>
  <rect x="101.5" y="101.5" width="821" height="821" rx="182.5" fill="none" stroke="#ffffff" stroke-opacity="0.12" stroke-width="3"/>
  <rect x="222" y="222" width="580" height="580" rx="66" fill="#f0783c" opacity="0.7" filter="url(#halo)"/>
  <rect x="212" y="212" width="600" height="600" rx="72" fill="#0b0b0c" stroke="#000000" stroke-width="2"/>
  <rect x="236" y="236" width="552" height="552" rx="56" fill="url(#face)"/>
  <path d="M292 238.5H732" stroke="#ffdcbe" stroke-opacity="0.5" stroke-width="3"/>
  <rect x="236.5" y="236.5" width="551" height="551" rx="55.5" fill="none" stroke="#000000" stroke-opacity="0.55" stroke-width="2"/>
  <rect x="0" y="0" width="1024" height="1024" fill="#1e0b03" fill-opacity="0.85" mask="url(#ink)"/>
</svg>
"""


def render(svg: Path, out: Path, size: int):
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), str(svg), "-o", str(out)], check=True)
    print(out.relative_to(ROOT), size)


if __name__ == "__main__":
    svg = ROOT / "Resources" / "Icon" / "OXIde-icon.svg"
    svg.parent.mkdir(parents=True, exist_ok=True)
    svg.write_text(SVG)
    render(svg, ROOT / "Resources" / "Icon" / "OXIde-1024.png", 1024)
    render(svg, ROOT / "docs" / "img" / "icon-64.png", 64)
    render(svg, ROOT / "docs" / "img" / "icon-180.png", 180)
