#!/bin/bash
# Double-click this file (inside the unzipped HumRig folder) to install HumRig for DaVinci Resolve.
cd "$(dirname "$0")" || exit 1
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$AU_DIR" "$VST_DIR"
rm -rf "$AU_DIR/HumRig.component" "$VST_DIR/HumRig.vst3"
cp -R Components/HumRig.component "$AU_DIR/"
cp -R VST3/HumRig.vst3 "$VST_DIR/"
# remove the macOS "downloaded from the internet" quarantine flag so the plugin is allowed to load
xattr -cr "$AU_DIR/HumRig.component" "$VST_DIR/HumRig.vst3"
codesign --force --deep --sign - "$AU_DIR/HumRig.component" 2>/dev/null
codesign --force --deep --sign - "$VST_DIR/HumRig.vst3" 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "HumRig installed."
echo "Now quit DaVinci Resolve completely (Cmd+Q) and open it again."
echo "In Fairlight: Mixer -> Effects (or the track's Effects slot) -> Audio Units / VST3 -> HumRig."
echo ""
read -n 1 -s -r -p "Press any key to close this window..."
