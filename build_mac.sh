#!/bin/bash
# Builds OSCar Editor (AU + VST3 + standalone app), runs the tests, then installs
# the plugins for your user. Usage: open Terminal in this folder and run  ./build_mac.sh
set -euo pipefail
cd "$(dirname "$0")"

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Apple's command line tools are missing. Run:  xcode-select --install"
    echo "then run ./build_mac.sh again."
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is missing. Install it with Homebrew:  brew install cmake"
    echo "(or download it from https://cmake.org/download/), then run ./build_mac.sh again."
    exit 1
fi

echo "== Building (the first run downloads JUCE and takes a few minutes)"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

echo "== Running the tests (nothing is installed if one fails)"
ctest --test-dir build --output-on-failure

ART="build/OSCarEditor_artefacts/Release"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DIR="$HOME/Applications"
mkdir -p "$AU_DIR" "$VST_DIR" "$APP_DIR"

install_bundle () {
    local src="$1" dst_dir="$2"
    local name; name="$(basename "$src")"
    rm -rf "$dst_dir/$name"
    cp -R "$src" "$dst_dir/"
    xattr -dr com.apple.quarantine "$dst_dir/$name" 2>/dev/null || true
    codesign --force --deep --sign - "$dst_dir/$name"
    echo "   installed $dst_dir/$name"
}

echo "== Installing"
install_bundle "$ART/AU/OSCar Editor.component" "$AU_DIR"
install_bundle "$ART/VST3/OSCar Editor.vst3" "$VST_DIR"
install_bundle "$ART/Standalone/OSCar Editor.app" "$APP_DIR"

# Make Logic/Live rescan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Done. Restart your DAW and look for 'OSCar Editor' (instrument)."
echo "The standalone app is in ~/Applications."
