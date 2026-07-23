#!/bin/bash
# Build icon/NebulaScope.icns from the .iconset PNGs.
#
# The sandbox that generated the PNGs can't write '@' in filenames, so the
# Retina variants are stored as '...-2x.png'. macOS iconutil REQUIRES the
# literal '@2x', so we stage a temp .iconset with the correct names first.
set -e
cd "$(dirname "$0")"

SRC="NebulaScope.iconset"
TMP="$(mktemp -d)/NebulaScope.iconset"
mkdir -p "$TMP"

for f in "$SRC"/*.png; do
    base="$(basename "$f")"
    # restore '-2x.png' -> '@2x.png'
    name="${base/-2x.png/@2x.png}"
    cp "$f" "$TMP/$name"
done

iconutil -c icns "$TMP" -o "NebulaScope.icns"
echo "Wrote icon/NebulaScope.icns"
