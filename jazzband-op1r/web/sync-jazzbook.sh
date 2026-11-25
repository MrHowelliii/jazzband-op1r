#!/bin/sh
# Build the web engine and copy it into a jazzbook checkout:  sh web/sync-jazzbook.sh ../../jazzbook
set -e
cd "$(dirname "$0")/.."
DEST=${1:?usage: sync-jazzbook.sh <jazzbook checkout>}
sh web/build.sh
mkdir -p out web/packs "$DEST/engine/packs"
cc -O2 -Imock web/pack2web.c -o out/pack2web
for p in jbpiano jbbass jbguitar jbdrums jbnylon; do
  [ -f "packs/out/$p.op1p" ] && out/pack2web "packs/out/$p.op1p" "web/packs/$p.jbp"
done
node web/test/wasm.test.mjs
cp web/jazzband.wasm "$DEST/engine/"
cp web/packs/*.jbp "$DEST/engine/packs/"
cp "../sound packs/CREDITS.md" "$DEST/engine/packs/CREDITS.md"
echo "copied to $DEST/engine"
