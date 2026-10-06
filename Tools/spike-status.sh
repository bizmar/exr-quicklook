#!/bin/bash
#
# Reports whether the two spike extensions are registered and enabled, and what
# UTI LaunchServices assigns to each fixture.
#
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

hr() { printf '\n\033[1m--- %s ---\033[0m\n' "$*"; }

hr "Registered Quick Look extensions, by extension point"
# NOT `pluginkit -p com.apple.quicklook.thumbnail`: that filter returns no
# matches on macOS 26.6.2 even though thumbnail extensions are registered.
# Filtering on the SDK field of the verbose listing is reliable.
pluginkit -mAvvv 2>/dev/null | awk '
  NF <= 2 && $NF ~ /\(.*\)$/ { id = $NF }   # record header: "[flag] bundleid(version)"
  /SDK = com.apple.quicklook.thumbnail/ { print "  [thumbnail] " id }
  /SDK = com.apple.quicklook.preview/   { print "  [preview]   " id }
' | grep -i bizmar.exr-quicklook || echo "  (none of ours registered)"

hr "Enablement  (+ enabled, - disabled, blank undecided)"
pluginkit -mAv 2>/dev/null | grep -i bizmar.exr-quicklook | cut -c1-72 || echo "  (not listed)"

hr "Detail"
for id in io.github.bizmar.exr-quicklook.Thumbnail io.github.bizmar.exr-quicklook.Preview; do
  out=$(pluginkit -mAvvv -i "$id" 2>/dev/null)
  if [ -z "$out" ]; then
    echo "  $id: NOT REGISTERED"
  else
    echo "$out" | grep -E 'io.github.bizmar.exr-quicklook|Path|SDK' | sed 's/^[[:space:]]*/  /'
  fi
done

hr "Declared content types"
for n in EXRThumbnail EXRQuickLook; do
  ax="$HOME/Applications/EXR Quick Look.app/Contents/PlugIns/$n.appex/Contents/Info.plist"
  [ -f "$ax" ] && printf '  %-14s %s\n' "$n" \
    "$(plutil -extract NSExtension.NSExtensionAttributes.QLSupportedContentTypes json -o - "$ax")"
done

hr "UTI assigned to each fixture by LaunchServices"
if [ -x "$ROOT/build/utitool" ]; then
  "$ROOT/build/utitool" "$ROOT"/Tests/Fixtures/spike/*
else
  echo "  (build/utitool missing -- run Tools/build-spike.sh)"
fi
