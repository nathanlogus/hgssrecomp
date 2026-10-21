#!/bin/bash
# Test helper for the merged view: runs an input script and tiles composed screenshots.
# usage: mshots.sh <snapshot or -> <script> <last frame> <shot frames> [extra args]
# The sheet is written to test/sheet.png; shots go to a new directory under test/runs/.
cd "$(dirname "$0")/.."
ROM=../pokeheartgold/build/soulsilver.us/pokesoulsilver.us.nds
mkdir -p test/runs
OUT="$(mktemp -d test/runs/XXXXXX)"
STATE=()
[ "$1" != "-" ] && STATE=(--state-load "$1")
./build/johto-online "$ROM" --headless --no-save "${STATE[@]}" --input "$2" --frames "$3" \
    --shot-dir "$OUT" --shot-at "$4" --compose 640x360 "${@:5}" 2>&1 | grep -v "^\[recomp\]"
python3 - "$OUT" <<'PY'
import glob, sys
from PIL import Image, ImageDraw
paths = sorted(glob.glob(sys.argv[1] + "/frame_*.png"))
cols = 4
rows = (len(paths) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 484, rows * 274), (60, 0, 60))
draw = ImageDraw.Draw(sheet)
for k, p in enumerate(paths):
    im = Image.open(p).resize((480, 270))
    x, y = (k % cols) * 484, (k // cols) * 274
    sheet.paste(im, (x, y))
    draw.text((x + 3, y + 2), p[-10:-4], fill=(255, 255, 0))
sheet.save("test/sheet.png")
PY
