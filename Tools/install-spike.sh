#!/bin/bash
#
# Installs EXR Quick Look into ~/Applications and forces LaunchServices to
# re-scan it, so PluginKit sees the two extensions.
#
# ~/Applications rather than /Applications: no sudo, and LaunchServices scans
# it on the same terms.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/build/EXR Quick Look.app"
DEST_DIR="$HOME/Applications"
DEST="$DEST_DIR/EXR Quick Look.app"
LEGACY="$DEST_DIR/EXRPreview.app"   # the name before 2026-10-06
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

[ -d "$SRC" ] || { echo "No build at $SRC -- run Tools/build-spike.sh first." >&2; exit 1; }

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

APPEXES=(EXRThumbnail EXRQuickLook)
appex_id() { plutil -extract CFBundleIdentifier raw "$1/Contents/PlugIns/$2.appex/Contents/Info.plist" 2>/dev/null; }
registered() { pluginkit -mA -i "$1" 2>/dev/null | grep -q "$1"; }

# Unregisters an installed copy's extensions and waits until PluginKit agrees.
# Removal is asynchronous: without the wait, pkd can process it *after* the
# re-registration below and silently drop the new extension -- which is what
# happened on 2026-10-06, leaving the thumbnail extension unregistered.
deregister() {
  local app="$1" id n
  for n in "${APPEXES[@]}"; do
    id=$(appex_id "$app" "$n") || continue
    pluginkit -r "$app/Contents/PlugIns/$n.appex" 2>/dev/null || true
    for _ in $(seq 1 20); do registered "$id" || break; sleep 0.5; done
  done
  "$LSREGISTER" -u "$app" 2>/dev/null || true
}

# Two installed copies with one bundle id would leave PluginKit choosing
# between them, so the old name is removed first.
if [ -d "$LEGACY" ]; then
  say "Removing the pre-rename install at $LEGACY"
  deregister "$LEGACY"
  rm -rf "$LEGACY"
fi

say "Installing to $DEST"
mkdir -p "$DEST_DIR"
# Only when the bundle ids change does the old registration need removing --
# otherwise re-registering the same ids in place is all that is needed, and
# removing them first only invites the race described above.
if [ -d "$DEST" ]; then
  for n in "${APPEXES[@]}"; do
    if [ "$(appex_id "$DEST" "$n")" != "$(appex_id "$SRC" "$n")" ]; then
      say "Bundle ids changed; unregistering the old ones"
      deregister "$DEST"
      break
    fi
  done
fi
rm -rf "$DEST"
cp -R "$SRC" "$DEST"
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

say "Registering with LaunchServices"
"$LSREGISTER" -f "$DEST"

# pluginkit -a returns success even when pkd ignores it, so check the result
# and retry rather than trusting the exit code.
say "Registering extensions with PluginKit"
missing=()
for n in "${APPEXES[@]}"; do
  id=$(appex_id "$DEST" "$n")
  for attempt in $(seq 1 10); do
    pluginkit -a "$DEST/Contents/PlugIns/$n.appex" 2>/dev/null || true
    registered "$id" && break
    [ "$attempt" -eq 5 ] && "$LSREGISTER" -f -R "$DEST"
    sleep 1
  done
  if registered "$id"; then echo "    registered $id"; else missing+=("$id"); fi
done
if [ ${#missing[@]} -gt 0 ]; then
  echo "FAILED: not registered with PluginKit: ${missing[*]}" >&2
  echo "Log out and back in (or reboot) and re-run; pkd sometimes holds stale state." >&2
  exit 1
fi

say "Done. Now run Tools/spike-status.sh"
