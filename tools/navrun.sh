#!/bin/bash
# Test helper: walk to waypoints with the navigator, then run a script.
# usage: navrun.sh <snapshot> <script> <last script frame> <waypoints> <shot every N frames> [extra args]
# Set NOMASH=--nav-stop-on-battle to hand battles to the script instead of mashing A.
# Shots (top-screen layout unless overridden) are tiled into test/sheet.png.
cd "$(dirname "$0")/.."
ROM=../pokeheartgold/build/soulsilver.us/pokesoulsilver.us.nds
mkdir -p test/runs
OUT="$(mktemp -d test/runs/XXXXXX)"
./build/johto-online "$ROM" --headless --no-save --state-load "$1" --input "$2" --frames "$3" \
    ${NOMASH:---battle-mash} --nav-map test/navmap.bin --nav "$4" --pos-every "$5" \
    --shot-dir "$OUT" --shot-every "$5" --compose 640x360 "${@:6}" 2>&1 | grep -v "^\[recomp\]"
python3 - "$OUT" <<'PY'
import glob, sys
from PIL import Image, ImageDraw
paths = sorted(glob.glob(sys.argv[1] + "/frame_*.png"))[-32:]
cols = 8
rows = (len(paths) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 244, rows * 138), (60, 0, 60))
draw = ImageDraw.Draw(sheet)
for k, p in enumerate(paths):
    im = Image.open(p).resize((240, 135))
    x, y = (k % cols) * 244, (k // cols) * 138
    sheet.paste(im, (x, y))
    draw.text((x + 3, y + 2), p[-10:-4], fill=(255, 255, 0))
sheet.save("test/sheet.png")
PY
