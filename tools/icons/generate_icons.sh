#!/bin/bash
#  games247: regenerate all app icons from es-app/assets/com.games247.app.svg (macOS only).
#  Run from the repository root: tools/icons/generate_icons.sh
set -e
SVG=es-app/assets/com.games247.app.svg
TMP=$(mktemp -d)
swiftc -O tools/icons/render_svg.swift -o "$TMP/render_svg"
R="$TMP/render_svg"

# macOS .icns (full 1024 grid with margin, as Apple expects)
mkdir "$TMP/games247.iconset"
for z in 16 32 128 256 512; do
  "$R" "$SVG" $z "$TMP/games247.iconset/icon_${z}x${z}.png"
  "$R" "$SVG" $((z*2)) "$TMP/games247.iconset/icon_${z}x${z}@2x.png"
done
iconutil -c icns "$TMP/games247.iconset" -o es-app/assets/games247.icns

# Windows .ico and SDL window icon (tile cropped to fill the image)
for z in 16 24 32 48 64 128 256; do "$R" "$SVG" $z "$TMP/$z.png" crop; done
python3 -I tools/icons/make_ico.py es-app/assets/games247.ico "$TMP"/{16,24,32,48,64,128,256}.png
"$R" "$SVG" 256 resources/graphics/window_icon_256.png crop

rm -rf "$TMP"
echo "Icons regenerated."
