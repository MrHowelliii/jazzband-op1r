#!/bin/sh
# Build the Jazz Band engine for the browser: web/jazzband.wasm (clang with the wasm32 target, no libc, no Emscripten).
set -e
cd "$(dirname "$0")/.."
clang --target=wasm32 -O2 -nostdlib -fno-builtin -ffreestanding -mbulk-memory \
  -Iweb -Imock -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=67108864 -Wl,-z,stack-size=262144 \
  -o web/jazzband.wasm web/jb_web.c
ls -l web/jazzband.wasm
