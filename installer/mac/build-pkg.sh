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
  /usr/libexec/PlistBuddy -c "Print" "$WORK/$id.plist" >/dev/null
  local n; n=$(/usr/libexec/PlistBuddy -c "Print" "$WORK/$id.plist" | grep -c "BundleIsRelocatable" || true)
  for ((i = 0; i < n; i++)); do /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/$id.plist"; done
  local extra=()
  [ -n "$scripts" ] && extra=(--scripts "$scripts")
  pkgbuild --root "$root" --component-plist "$WORK/$id.plist" --identifier "com.sonicology.$id" --version "$VERSION" \
           --install-location / "${extra[@]}" "$WORK/pkgs/$id.pkg"
}

plugins () {   # the plug-ins of one product, signed ad hoc, laid out as they install
  local name="$1" id="$2" root="$WORK/root-$id"
  mkdir -p "$root/Library/Audio/Plug-Ins/VST3" "$root/Library/Audio/Plug-Ins/Components"
  cp -R "$STAGE/$name/VST3/"*.vst3 "$root/Library/Audio/Plug-Ins/VST3/"
  if compgen -G "$STAGE/$name/AU/*.component" >/dev/null; then cp -R "$STAGE/$name/AU/"*.component "$root/Library/Audio/Plug-Ins/Components/"; fi
  find "$root/Library/Audio/Plug-Ins" -maxdepth 2 \( -name "*.vst3" -o -name "*.component" \) -print0 |
    while IFS= read -r -d '' b; do codesign --force --deep --sign - "$b"; done
  component "$id" "$root" "$WORK/scripts-au"
}

# After the plug-ins: macOS rereads the Audio Units (Logic finds them at its next start).
mkdir -p "$WORK/scripts-au"
cat > "$WORK/scripts-au/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x "$WORK/scripts-au/postinstall"

plugins Voxology voxology
plugins Mixology mixology
plugins Masterology masterology

# The Cubase bridge script: kept in /Library/Application Support/Sonicology, copied into the user's Cubase folder.
CUB="$WORK/root-cubase/Library/Application Support/Sonicology/Cubase MIDI Remote"
mkdir -p "$CUB" "$WORK/scripts-cubase"
cp -R "$STAGE/Mixology/Cubase MIDI Remote/Mixology" "$CUB/"
cat > "$WORK/scripts-cubase/postinstall" <<'EOF'
#!/bin/sh
U=$(stat -f%Su /dev/console)
[ -z "$U" ] || [ "$U" = "root" ] && exit 0
H=$(dscl . -read "/Users/$U" NFSHomeDirectory | awk '{print $2}')
D="$H/Documents/Steinberg/Cubase/MIDI Remote/Driver Scripts/Local"
sudo -u "$U" mkdir -p "$D"
rm -rf "$D/Mixology"
sudo -u "$U" cp -R "/Library/Application Support/Sonicology/Cubase MIDI Remote/Mixology" "$D/"
exit 0
EOF
chmod +x "$WORK/scripts-cubase/postinstall"
component cubase "$WORK/root-cubase" "$WORK/scripts-cubase"

# The Read Me and the manuals, in /Applications/Sonicology.
DOCS="$WORK/root-docs/Applications/Sonicology"
mkdir -p "$DOCS"
cp "$STAGE/Read Me First.txt" "$DOCS/"
cp -R "$STAGE/Manuals" "$DOCS/"
component docs "$WORK/root-docs"

# The installer's pages.
cp "$HERE/../art/mac-background.png" "$WORK/resources/background.png"
sed -e "s|@LABEL@|$LABEL|g" "$HERE/welcome.html" > "$WORK/resources/welcome.html"
cp "$STAGE/Read Me First.txt" "$WORK/resources/readme.txt"
sed -e "s|@VERSION@|$VERSION|g" -e "s|@LABEL@|$LABEL|g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK/pkgs" \
             "$OUT/Sonicology-Installer-macOS.pkg"
echo "Built $OUT/Sonicology-Installer-macOS.pkg"
