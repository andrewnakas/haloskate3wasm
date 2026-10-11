#!/bin/sh
# Assemble the Pages site: the engine's web build + our overlay (site/).
# Maps are not hosted here: clean/maps.json points at the halo-cleanroom repos (same origin).
set -e
cd "$(dirname "$0")/.."
OUT=${1:-dist}
rm -rf "$OUT"; mkdir -p "$OUT"
cp -R engine/build/web/site/. "$OUT/"
cp -R site/. "$OUT/"
# Refresh the manifest from the live clean-room site when reachable (keeps sha256s current).
if curl -fsS https://andrewnakas.github.io/halo-cleanroom/clean/maps.json -o /tmp/hs3-maps.json 2>/dev/null; then
  python3 tools/rebase_maps.py /tmp/hs3-maps.json "$OUT/clean/maps.json"
fi
touch "$OUT/.nojekyll"
du -sh "$OUT"
