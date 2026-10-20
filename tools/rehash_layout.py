#!/usr/bin/env python3
"""Makes a layout for a ROM hack whose code sections changed in place (same addresses and
sizes, e.g. Definitive HeartGold) from the layout of the game it patches: the code/data
map is kept and only the checksums are taken from the hack's ROM.

usage: rehash_layout.py <base layout> <hack ROM> <new layout> <game name>

Code the hack adds outside the mapped code (loaded at run time, or put where the base
game has data) is not recompiled: the runtime runs it on the interpreter.
"""
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ndsrom import read_rom
from recomp import autoload_image

base_path, rom_path, out_path, game = sys.argv[1:5]
layout = json.load(open(base_path))
main_image, overlays = read_rom(rom_path)
for entry in layout['sections']:
    src = entry['source']
    if src == 'main':
        image = main_image
    elif src == 'itcm':
        image = autoload_image(main_image, entry['base'])
    else:
        image = overlays[src][1]
    # A section that moved or changed size no longer fits the map: that needs make_layout.py.
    if len(image) != entry['size']:
        raise SystemExit(f'section {entry["name"]} changed size ({entry["size"]} -> {len(image)} bytes)')
    digest = hashlib.sha1(image).hexdigest()
    if digest != entry['sha1']:
        print(f'{entry["name"]}: changed')
    entry['sha1'] = digest
layout['game'] = game
layout['rom_sha1'] = hashlib.sha1(open(rom_path, 'rb').read()).hexdigest()
with open(out_path, 'w') as f:
    json.dump(layout, f, separators=(',', ':'))
