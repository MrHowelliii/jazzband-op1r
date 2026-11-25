#!/bin/sh
# Build the web engine and its sound packs and copy them into a jazzbook checkout:
#   sh web/sync-jazzbook.sh ../../jazzbook [sfz sources dir]
# With a sources dir the packs are rebuilt (web/make_web_packs.py); without, the ones in web/packs are used.
set -e
cd "$(dirname "$0")/.."
DEST=${1:?usage: sync-jazzbook.sh <jazzbook checkout> [sfz sources]}
sh web/build.sh
[ -n "$2" ] && python3 web/make_web_packs.py "$2" web/packs
node web/test/wasm.test.mjs
mkdir -p "$DEST/engine/packs"
cp web/jazzband.wasm "$DEST/engine/"
cp web/packs/*.jbp "$DEST/engine/packs/"
cp web/PACK-CREDITS.md "$DEST/engine/packs/CREDITS.md"
echo "copied to $DEST/engine"
