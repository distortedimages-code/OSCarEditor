#!/bin/bash
# OSCar Editor: installs the plug-in (AU, VST3, app), unblocks it and checks the AU for Logic Pro.
# Use: open Terminal, type "bash" and a space, drop this file into the window, press Return.
#
# Each bundle goes where a copy already is (/Library, from the DMG shortcuts), otherwise into
# your own Library (no password needed). Copies that are already up to date are left alone, so
# running it after dragging the files only unblocks and checks them.

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="OSCar Editor"
problems=0

if defaults read -g AppleLocale 2>/dev/null | grep -qi '^fr'; then FR=1; else FR=0; fi
t() { if [ "$FR" = 1 ]; then printf '%s' "$2"; else printf '%s' "$1"; fi; }
good() { printf '  \033[32m✓\033[0m %s\n' "$1"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$1"; problems=$((problems + 1)); }

version_of() {
    /usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$1/Contents/Info.plist" 2>/dev/null
}

# $1 bundle file name, $2 system folder, $3 own folder (empty: system folder only)
install_bundle() {
    local file="$1" sys="$2" own="$3" src="$HERE/$1" dest
    if [ -e "$sys/$file" ] || [ -z "$own" ]; then dest="$sys"; else dest="$own"; fi

    if [ -d "$src" ]; then
        if [ ! -e "$dest/$file" ] || [ "$(version_of "$src")" != "$(version_of "$dest/$file")" ]; then
            mkdir -p "$dest" 2>/dev/null
            rm -rf "${dest:?}/$file" 2>/dev/null
            if ! ditto "$src" "$dest/$file" 2>/dev/null; then
                bad "$file: $(t "can't write to $dest. Run it again with sudo before bash." "impossible d'écrire dans $dest. Relance avec sudo devant bash.")"
                return
            fi
        fi
    elif [ ! -e "$dest/$file" ]; then
        bad "$file: $(t "not found. Run this from the OSCar Editor disk image." "introuvable. Lance ce fichier depuis l'image disque d'OSCar Editor.")"
        return
    fi

    if ! xattr -dr com.apple.quarantine "$dest/$file" 2>/dev/null; then
        bad "$file: $(t "can't unblock it. Run it again with sudo before bash." "impossible de le débloquer. Relance avec sudo devant bash.")"
        return
    fi
    good "$file  →  $dest  ($(version_of "$dest/$file"))"
}

echo
echo "OSCar Editor $(version_of "$HERE/$NAME.component")"
echo "$(t "Installing and unblocking" "Installation et déblocage")"
install_bundle "$NAME.component" "/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/Components"
install_bundle "$NAME.vst3"      "/Library/Audio/Plug-Ins/VST3"       "$HOME/Library/Audio/Plug-Ins/VST3"
install_bundle "$NAME.app"       "/Applications"                      ""

# two copies of the same plug-in confuse the host: say which one to remove
for kind in "Components/$NAME.component" "VST3/$NAME.vst3"; do
    if [ -e "/Library/Audio/Plug-Ins/$kind" ] && [ -e "$HOME/Library/Audio/Plug-Ins/$kind" ]; then
        bad "$(t "Two copies: remove" "Deux copies : supprime") ~/Library/Audio/Plug-Ins/$kind"
    fi
done

echo
echo "$(t "Checking the Audio Unit for Logic Pro (a few seconds)" "Vérification de l'Audio Unit pour Logic Pro (quelques secondes)")"
killall -9 AudioComponentRegistrar 2>/dev/null   # makes macOS list the new version
if auval -v aumu Oscr Otls 2>&1 | grep -q "AU VALIDATION SUCCEEDED"; then
    good "$(t "Audio Unit validated: Logic Pro can load it." "Audio Unit validée : Logic Pro peut la charger.")"
else
    bad "$(t "The Audio Unit check failed. Restart the Mac and run this again; in Logic Pro, Settings > Plug-in Manager > Reset & Rescan Selection." \
            "La vérification de l'Audio Unit a échoué. Redémarre le Mac et relance ce fichier ; dans Logic Pro : Réglages > Gestionnaire de module > Réinitialiser et réanalyser la sélection.")"
fi

echo
if [ "$problems" = 0 ]; then
    echo "$(t "Done. Restart Logic Pro (or your DAW): OSCar Editor is in the AU instruments, under OSCarTools." \
              "Terminé. Relance Logic Pro (ou ton DAW) : OSCar Editor est dans les instruments AU, rubrique OSCarTools.")"
else
    echo "$(t "Finished with $problems problem(s), see above." "Terminé avec $problems problème(s), voir ci-dessus.")"
fi
echo
