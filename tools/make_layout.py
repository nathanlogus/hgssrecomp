#!/usr/bin/env python3
"""Makes the layout file that lets the recompiler work from a player's own ROM
instead of from the decompilation's build (see tools/recomp.py --rom).

usage: make_layout.py <build dir> <built ROM> <layout file> [game name]

The layout holds, for every code section: its name, load address, where in the ROM
its bytes come from, a SHA-1 of them (to check a player's ROM), which address ranges
are ARM or Thumb code, and the addresses of named symbols; plus the addresses of the
functions the frontend watches. It holds no code or data of the game itself.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ndsrom import read_rom
from recomp import load_sections

build_dir, rom_path, out_path = sys.argv[1:4]
game = sys.argv[4] if len(sys.argv) > 4 else 'Pokemon SoulSilver (USA)'
main_image, overlays = read_rom(rom_path)
by_hash = {hashlib.sha1(data).hexdigest(): ovy for ovy, (ram, data) in overlays.items()}

sections = []
for sec in load_sections(build_dir):
    digest = hashlib.sha1(sec.image).hexdigest()
    if sec.name == 'main':
        source = 'main'
        assert sec.image == main_image, 'main image differs from the ROM'
    elif sec.name == 'ITCM':
        source = 'itcm'
    elif digest in by_hash:
        source = by_hash[digest]
    else:
        raise SystemExit(f'section {sec.name} is not in the ROM')
    sections.append({
        'name': sec.name, 'base': sec.base, 'source': source, 'size': len(sec.image), 'sha1': digest,
        'ranges': [[s, e, int(t)] for s, e, t in sec.ranges],
        'named': sorted(sec.named),
    })

# The applications and watched functions: as tools/apps.py finds them in the build.
with tempfile.TemporaryDirectory() as tmp:
    subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'apps.py'),
                    build_dir, tmp], check=True, stdout=subprocess.DEVNULL)
    table = open(os.path.join(tmp, 'apps_table.cpp')).read()
row = re.compile(r'\{"(\w+)", "(\w+)", 0x([0-9A-F]{8})u\}')
app_row = re.compile(r'\{"(\w+)", "(\w+)", 0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, "(\w*)", 0x([0-9A-F]{8})u, "(\w*)"\}')
apps_part, focus_part = table.split('rc_focus_syms')
apps = [[m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16), m.group(5), int(m.group(6), 16), m.group(7)]
        for m in app_row.finditer(apps_part)]
focus = [[m.group(1), m.group(2), int(m.group(3), 16)] for m in row.finditer(focus_part)]

layout = {
    'game': game,
    'rom_sha1': hashlib.sha1(open(rom_path, 'rb').read()).hexdigest(),
    'sections': sections,
    'apps': apps,
    'focus': focus,
}
with open(out_path, 'w') as f:
    json.dump(layout, f, separators=(',', ':'))
print(f'{len(sections)} sections, {len(apps)} applications, {len(focus)} watched functions -> {out_path}')
