#!/bin/sh
# Needs: clang (wasm32 target), wasm-ld, wasm-component-ld
set -e
clang --target=wasm32-unknown-unknown -O2 -nostdlib -ffreestanding -fno-builtin -c recipix.c -o recipix.o
wasm-component-ld -o mod.wasm recipix.o --no-entry --export=cabi_realloc --component-type recipix.wit --string-encoding utf8
python3 -c "import zipfile;z=zipfile.ZipFile('recipix-0.1.0.hmod','w',zipfile.ZIP_DEFLATED);z.write('horizon-mod.toml');z.write('mod.wasm')"
