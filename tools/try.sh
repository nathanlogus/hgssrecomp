#!/bin/bash
# Test helper: resume from a snapshot, run an input script, and tile screenshots.
# usage: try.sh <snapshot> <script> <last frame> <shot frames, comma separated> [extra args]
# Screenshots go to a new directory under test/runs/ each time; the sheet is test/sheet.png.
cd "$(dirname "$0")/.."
ROM=../pokeheartgold/build/soulsilver.us/pokesoulsilver.us.nds
mkdir -p test/runs
OUT="$(mktemp -d test/runs/XXXXXX)"
./build/johto-online "$ROM" --headless --no-save --state-load "$1" --input "$2" --frames "$3" \
    --shot-dir "$OUT" --shot-at "$4" "${@:5}" 2>&1 | tail -1
python3 tools/sheet.py test/sheet.png "$OUT"/*.png
