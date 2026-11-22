#!/bin/bash
# Installs Red Trigger (AU + VST3 + Standalone) for the current user.
set -e
cd "$(dirname "$0")"

COMP="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"
mkdir -p "$COMP" "$VST3" "$APPS"

rm -rf "$COMP/Red Trigger.component" "$VST3/Red Trigger.vst3" "$APPS/Red Trigger.app"
cp -R "AU/Red Trigger.component" "$COMP/"
cp -R "VST3/Red Trigger.vst3" "$VST3/"
cp -R "Standalone/Red Trigger.app" "$APPS/"

# These builds aren't notarized by Apple, so clear the "downloaded from the internet" flag.
xattr -dr com.apple.quarantine "$COMP/Red Trigger.component" "$VST3/Red Trigger.vst3" "$APPS/Red Trigger.app" 2>/dev/null || true

# Make macOS re-scan Audio Units.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Red Trigger installed:"
echo "  AU:         $COMP/Red Trigger.component"
echo "  VST3:       $VST3/Red Trigger.vst3"
echo "  Standalone: $APPS/Red Trigger.app"
echo
echo "Restart your DAW (in Logic, use Plug-in Manager > Reset & Rescan if it doesn't appear)."
