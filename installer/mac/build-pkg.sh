#!/bin/bash
# Sonicology for macOS: one installer (.pkg) for Voxology (+ Honey Tune), Mixology (+ Mixology Link) and Masterology.
#   installer/mac/build-pkg.sh <stage dir> <suite version> <label> <out dir>
# <stage>/<Plug-in>/VST3/*.vst3 and <stage>/<Plug-in>/AU/*.component, as the suite workflow stages them.
# Unsigned for the beta: every bundle is signed ad hoc (Apple Silicon needs a signature to load), the package
# itself isn't, so macOS asks once (System Settings > Privacy & Security > Open Anyway).
set -euo pipefail
STAGE="$1"; VERSION="$2"; LABEL="$3"; OUT="$4"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
mkdir -p "$OUT" "$WORK/pkgs" "$WORK/resources"

# One component package per choice; the bundles never move ("relocatable" would let macOS install an update
# wherever an older copy happens to be).
component () {
  local id="$1" root="$2" scripts="${3:-}"
  pkgbuild --analyze --root "$root" "$WORK/$id.plist" >/dev/null
  local i=0
  while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/$id.plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/$id.plist" 2>/dev/null ||
      /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/$id.plist"   # not every entry has it
    i=$((i + 1))
  done
  # macOS's bash 3.2 calls an empty array unbound under set -u: two calls instead
  if [ -n "$scripts" ]; then
    pkgbuild --root "$root" --component-plist "$WORK/$id.plist" --identifier "com.sonicology.$id" --version "$VERSION" \
             --install-location / --scripts "$scripts" "$WORK/pkgs/$id.pkg"
  else
    pkgbuild --root "$root" --component-plist "$WORK/$id.plist" --identifier "com.sonicology.$id" --version "$VERSION" \
             --install-location / "$WORK/pkgs/$id.pkg"
  fi
}

plugins () {   # the plug-ins of one product, signed ad hoc, laid out as they install
  local name="$1" id="$2" scripts="${3:-$WORK/scripts-au}"
  local root="$WORK/root-$id"   # its own line: "local" expands every word before it sets any
  mkdir -p "$root/Library/Audio/Plug-Ins/VST3" "$root/Library/Audio/Plug-Ins/Components"
  cp -R "$STAGE/$name/VST3/"*.vst3 "$root/Library/Audio/Plug-Ins/VST3/"
  if compgen -G "$STAGE/$name/AU/*.component" >/dev/null; then cp -R "$STAGE/$name/AU/"*.component "$root/Library/Audio/Plug-Ins/Components/"; fi
  # GitHub's artifact zips drop the executable bit: give it back to each bundle's program before signing
  find "$root/Library/Audio/Plug-Ins" -path "*/Contents/MacOS/*" -type f -exec chmod 755 {} +
  find "$root/Library/Audio/Plug-Ins" -maxdepth 2 \( -name "*.vst3" -o -name "*.component" \) -print0 |
    while IFS= read -r -d '' b; do codesign --force --deep --sign - "$b"; done
  component "$id" "$root" "$scripts"
}

# After the plug-ins: macOS rereads the Audio Units (Logic finds them at its next start).
mkdir -p "$WORK/scripts-au"
cat > "$WORK/scripts-au/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x "$WORK/scripts-au/postinstall"

# Masterology's first name was Polisher, with the same plug-in IDs: an old Polisher bundle is the same plug-in to
# the DAW (it might load the old one), so it goes.
mkdir -p "$WORK/scripts-masterology"
cat > "$WORK/scripts-masterology/postinstall" <<'SCRIPT'
#!/bin/sh
user=$(stat -f%Su /dev/console 2>/dev/null)
home=$(dscl . -read "/Users/$user" NFSHomeDirectory 2>/dev/null | awk '{print $2}')
for d in "/Library/Audio/Plug-Ins" "${home:+$home/Library/Audio/Plug-Ins}"; do
  [ -n "$d" ] && rm -rf "$d/VST3/Polisher.vst3" "$d/Components/Polisher.component" 2>/dev/null
done
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
SCRIPT
chmod +x "$WORK/scripts-masterology/postinstall"

plugins Voxology voxology
plugins Mixology mixology
plugins Masterology masterology "$WORK/scripts-masterology"

# The bridge scripts: kept in /Library/Application Support/Sonicology, copied into the user's DAW folders
# (they live in the user's own Documents / Music folder, which an installer running as root only reaches this way).
userscript () {   # <id> <source folder in the stage> <folder name> <destination, relative to the user's home>
  local id="$1" src="$2" name="$3" dest="$4"
  local keep="$WORK/root-$id/Library/Application Support/Sonicology/$id"
  mkdir -p "$keep" "$WORK/scripts-$id"
  cp -R "$src" "$keep/"
  cat > "$WORK/scripts-$id/postinstall" <<SCRIPT
#!/bin/sh
U=\$(stat -f%Su /dev/console)
[ -z "\$U" ] || [ "\$U" = "root" ] && exit 0
H=\$(dscl . -read "/Users/\$U" NFSHomeDirectory | awk '{print \$2}')
D="\$H/$dest"
sudo -u "\$U" mkdir -p "\$D"
rm -rf "\$D/$name"
sudo -u "\$U" cp -R "/Library/Application Support/Sonicology/$id/$name" "\$D/"
exit 0
SCRIPT
  chmod +x "$WORK/scripts-$id/postinstall"
  component "$id" "$WORK/root-$id" "$WORK/scripts-$id"
}
userscript cubase "$STAGE/Mixology/Cubase MIDI Remote/Mixology" "Mixology" "Documents/Steinberg/Cubase/MIDI Remote/Driver Scripts/Local"
userscript flstudio "$STAGE/Mixology/FL Studio/Mixology Bridge" "Mixology Bridge" "Documents/Image-Line/FL Studio/Settings/Hardware"
userscript ableton "$STAGE/Mixology/Ableton/MixologyBridge" "MixologyBridge" "Music/Ableton/User Library/Remote Scripts"

# The Read Me and the manuals, in /Applications/Sonicology.
DOCS="$WORK/root-docs/Applications/Sonicology"
mkdir -p "$DOCS"
cp "$STAGE/Read Me First.txt" "$DOCS/"
cp -R "$STAGE/Manuals" "$DOCS/"
component docs "$WORK/root-docs"

# The installer's pages.
cp "$HERE/../art/mac-background.png" "$WORK/resources/background.png"
cp "$HERE/../art/title.png" "$WORK/resources/title.png"   # the Sonicology title on the welcome page
sed -e "s|@LABEL@|$LABEL|g" "$HERE/welcome.html" > "$WORK/resources/welcome.html"
cp "$STAGE/Read Me First.txt" "$WORK/resources/readme.txt"
sed -e "s|@VERSION@|$VERSION|g" -e "s|@LABEL@|$LABEL|g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK/pkgs" \
             "$OUT/Sonicology-Installer-macOS.pkg"
echo "Built $OUT/Sonicology-Installer-macOS.pkg"
