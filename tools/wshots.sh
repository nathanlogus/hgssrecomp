#!/bin/bash
# Like mshots.sh, but through the real window path: the game runs fullscreen inside a
# headless gamescope and the screenshots are read back from the renderer.
# usage: wshots.sh <snapshot or -> <script> <last frame> <shot frames> [extra args]
# Environment: SIZE=2560x1440 (default), CFG=<settings file> (default: a copy of johto-online.cfg).
cd "$(dirname "$0")/.."
ROM=../pokeheartgold/build/soulsilver.us/pokesoulsilver.us.nds
SIZE=${SIZE:-2560x1440}
W=${SIZE%x*}; H=${SIZE#*x}
mkdir -p test/runs
OUT="$(mktemp -d test/runs/XXXXXX)"
if [ -z "$CFG" ]; then CFG="$OUT/test.cfg"; cp johto-online.cfg "$CFG" 2>/dev/null; fi
STATE=()
[ "$1" != "-" ] && STATE=(--state-load "$1")
SSR_PERF=1 SSR_NO_PAD=1 timeout 300 gamescope --backend headless -W $W -H $H -w $W -h $H -r 144 -- \
    ./build/johto-online "$ROM" --no-save --config "$CFG" --fullscreen "${STATE[@]}" --input "$2" --frames "$3" \
    --shot-dir "$OUT" --shot-at "$4" "${@:5}" > "$OUT/log.txt" 2>&1
grep "^\[perf\] input" "$OUT/log.txt" | tail -1
python3 - "$OUT" <<'PY'
import glob, sys
from PIL import Image, ImageDraw
paths = sorted(glob.glob(sys.argv[1] + "/window_*.png"))
cols = 4
rows = (len(paths) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 484, rows * 274), (60, 0, 60))
draw = ImageDraw.Draw(sheet)
for k, p in enumerate(paths):
    im = Image.open(p).convert("RGB").resize((480, 270), Image.LANCZOS)
    x, y = (k % cols) * 484, (k // cols) * 274
    sheet.paste(im, (x, y))
    draw.text((x + 3, y + 2), p[-10:-4], fill=(255, 255, 0))
sheet.save("test/sheet.png")
print(len(paths), "shots in", sys.argv[1])
PY
