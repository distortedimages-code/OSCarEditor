#!/bin/bash
# Builds the installer disk image (macOS only): the three bundles, shortcuts to their folders,
# and the window background.
#   tools/dmg/make_dmg.sh <folder holding the three bundles> <output .dmg>
# Needs dmgbuild:  python3 -m pip install dmgbuild==1.6.7
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$1" && pwd)"
OUT="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for b in "OSCar Editor.component" "OSCar Editor.vst3" "OSCar Editor.app"; do
    [ -d "$SRC/$b" ] || { echo "missing: $SRC/$b"; exit 1; }
done
cp "$HERE/background.png" "$HERE/background@2x.png" "$WORK/"

rm -f "$OUT"
python3 -m dmgbuild -s "$HERE/dmg_settings.py" -D src="$SRC" -D work="$WORK" "OSCar Editor" "$OUT"
hdiutil verify "$OUT"
echo "built $OUT"
