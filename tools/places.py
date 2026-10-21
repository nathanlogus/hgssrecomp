#!/usr/bin/env python3
"""Test places: start the game straight at a chosen spot and look at it or test it there.

    places.py save <in.sav> <out.sav> <place | map,x,y[,dir]>
        a copy of the save in which Continue takes the player to that place
    places.py snapshots <rom> <save> [--only a,b] [--time day,night]
        boots the game once per place in tools/test_places.json and keeps a snapshot of
        the moment it arrives in test/places/snapshots/<name>.st (with a picture, <name>.png);
        other times of day than "day" go to <name>.<time>.st
    places.py run <rom> [--only a,b] [--time day,night] [--set key=value ...] [--frames N] [--size WxH]
        loads every snapshot with these settings (as in johto-online.cfg, e.g. zoom=60),
        walks the place's route and tiles the screenshots into test/places/sheets/sheet.png,
        one row per place and time of day (the screenshots stay in test/places/runs/)
    places.py lockstep <rom> [--only a,b] [--time day,night] [--frames N]
        runs every snapshot with --lockstep (recompiled code against the interpreter); the
        logs go to test/places/lockstep/

Times of day: day (12:00), morning (07:00), evening (18:00), night (22:00). The game
clock is part of a snapshot, so each time of day has snapshots of its own.

The save trick: Continue normally rebuilds the map from the saved objects, but when save
flag 0x966 is set (the game sets it after the Hall of Fame) it warps the player to the
saved "dynamic warp" location instead and sets that map up afresh. Offsets are those of
HeartGold/SoulSilver (US); Definitive HeartGold uses the same save.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'test', 'places')


def place_dir(kind):
    """test/places/<kind>: saves, snapshots, runs, lockstep, sheets, work."""
    d = os.path.join(OUT, kind)
    os.makedirs(d, exist_ok=True)
    return d
EXE = os.path.join(ROOT, 'build', 'ssrecomp.exe' if os.name == 'nt' else 'ssrecomp')

SLOTS = (0, 0x40000)          # the two copies of the save
BLOCK = 0xF628                # the general block, its footer in the last 16 bytes
MAGIC = 0x20060623
FLAGS = 0x10C4                # SaveVarsFlags.flags
FLAG_WARP_ON_CONTINUE = 0x966
DYNAMIC_WARP = 0x1270         # LocalFieldData.dynamicWarp (a Location: map, warp, x, y, dir)

TIMES = {'day': '12:00', 'morning': '07:00', 'evening': '18:00', 'night': '22:00'}

ARRIVE = 1900                 # frame of the snapshot: the place is shown well before this
# A through the intro, the title and the Continue menu, which can come as late as frame
# ~1150; afterwards an A in the field faces away from the door the place starts at.
BOOT_TAPS = range(400, 1400, 90)


def snapshot_frame(path):
    """The frame a snapshot was taken at (its first word; older ones are at 1500)."""
    with open(path, 'rb') as f:
        return struct.unpack('<I', f.read(4))[0]


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def load_places():
    return json.load(open(os.path.join(HERE, 'test_places.json')))['places']


def parse_place(text):
    for p in load_places():
        if p['name'] == text:
            return p
    parts = [int(v) for v in text.split(',')]
    if len(parts) not in (3, 4):
        raise SystemExit(f'not a place name or map,x,y[,dir]: {text}')
    return {'name': text, 'map': parts[0], 'x': parts[1], 'y': parts[2], 'dir': parts[3] if len(parts) > 3 else 1}


def make_save(src, dst, place):
    data = bytearray(open(src, 'rb').read())
    # The newer of the two valid copies is the one the game loads.
    best = None
    for base in SLOTS:
        count, size, magic = struct.unpack_from('<III', data, base + BLOCK - 16)
        if magic == MAGIC and size == BLOCK and crc16(data[base:base + BLOCK - 16]) == struct.unpack_from('<H', data, base + BLOCK - 2)[0]:
            if best is None or count > best[1]:
                best = (base, count)
    if best is None:
        raise SystemExit(f'{src}: no valid HeartGold/SoulSilver save in it')
    base = best[0]
    data[base + FLAGS + FLAG_WARP_ON_CONTINUE // 8] |= 1 << (FLAG_WARP_ON_CONTINUE % 8)
    struct.pack_into('<iiiii', data, base + DYNAMIC_WARP, place['map'], -1, place['x'], place['y'], place.get('dir', 1))
    struct.pack_into('<H', data, base + BLOCK - 2, crc16(data[base:base + BLOCK - 16]))
    with open(dst, 'wb') as f:
        f.write(data)


def selected(only):
    places = load_places()
    if only:
        names = only.split(',')
        places = [p for p in places if p['name'] in names]
        if not places:
            raise SystemExit(f'no such place: {only}')
    return places


def run_game(args, log, time='day'):
    # The game clock the console starts with (a snapshot brings its own back; None leaves it
    # alone, as --lockstep needs: it sets the clock of only one of its two consoles).
    env = dict(os.environ)
    if time:
        env['SSR_CLOCK'] = '2026-01-01 ' + TIMES[time]
    with open(log, 'w') as f:
        return subprocess.run([EXE] + args, stdout=f, stderr=subprocess.STDOUT, env=env).returncode


def times(text):
    out = text.split(',')
    for t in out:
        if t not in TIMES:
            raise SystemExit(f'no such time of day: {t} (one of {", ".join(TIMES)})')
    return out


def snap_name(p, time):
    """File name (without extension) of a place's snapshot at a time of day."""
    return p['name'] if time == 'day' else f'{p["name"]}.{time}'


def write_input(path, lines):
    with open(path, 'w') as f:
        f.write(''.join(l + '\n' for l in lines))


def cmd_snapshots(a):
    os.makedirs(OUT, exist_ok=True)
    for p in selected(a.only):
        sav = os.path.join(place_dir('saves'), p['name'] + '.sav')
        make_save(a.save, sav, p)
        boot = os.path.join(place_dir('work'), 'boot.txt')
        write_input(boot, [f'{f} {f + 5} A' for f in BOOT_TAPS])
        for t in times(a.time):
            name = snap_name(p, t)
            shots = os.path.join(place_dir('work'), 'tmp')
            shutil.rmtree(shots, ignore_errors=True)
            os.makedirs(shots)
            code = run_game([a.rom, '--headless', '--save', sav, '--no-save', '--input', boot,
                             '--frames', str(ARRIVE + 2), '--state-save', os.path.join(place_dir('snapshots'), name + '.st'),
                             '--state-at', str(ARRIVE), '--shot-dir', shots + '/', '--shot-at', str(ARRIVE),
                             '--compose', '640x360'], os.path.join(place_dir('snapshots'), name + '.log'), t)
            shot = os.path.join(shots, f'frame_{ARRIVE:06d}.png')
            if os.path.exists(shot):
                shutil.move(shot, os.path.join(place_dir('snapshots'), name + '.png'))
            shutil.rmtree(shots, ignore_errors=True)
            print(f'{name}: {"ok" if code == 0 else "exit " + str(code)} -> test/places/snapshots/{name}.png')


def walk_input(p, start):
    lines, f = [], start + 10
    for key, frames in p.get('walk', []):
        lines.append(f'{f} {f + frames} {key}')
        f += frames + 5
    return lines, f


def cmd_run(a):
    cfg = os.path.join(place_dir('work'), 'run.cfg')
    with open(cfg, 'w') as f:
        f.write('view=merged\n' + ''.join(s + '\n' for s in a.set))
    w, h = (int(v) for v in a.size.split('x'))
    sheet_rows = []
    for p in selected(a.only):
        for t in times(a.time):
            name = snap_name(p, t)
            snap = os.path.join(place_dir('snapshots'), name + '.st')
            if not os.path.exists(snap):
                print(f'{name}: no snapshot (run "snapshots --time {t}" first)')
                continue
            start = snapshot_frame(snap)
            lines, end = walk_input(p, start)
            end = max(end, start + a.frames)
            inp = os.path.join(place_dir('work'), 'walk.txt')
            write_input(inp, lines)
            shots = os.path.join(place_dir('runs'), name)
            shutil.rmtree(shots, ignore_errors=True)
            os.makedirs(shots)
            at = [start + 30 + (end - start - 30) * k // 3 for k in range(4)]
            code = run_game([a.rom, '--headless', '--no-save', '--save', os.path.join(place_dir('saves'), p['name'] + '.sav'),
                             '--config', cfg, '--state-load', snap, '--input', inp, '--frames', str(end + 1),
                             '--shot-dir', shots + '/', '--shot-at', ','.join(map(str, at)), '--compose', a.size],
                            os.path.join(shots, 'game.log'), t)
            label = p['name'] if t == 'day' and a.time == 'day' else f'{p["name"]} ({t})'
            sheet_rows.append((label, p['check'], [os.path.join(shots, f'frame_{f:06d}.png') for f in at]))
            print(f'{name}: {"ok" if code == 0 else "exit " + str(code)}  ({p["check"]})')
    make_sheet(sheet_rows, w, h)


def make_sheet(rows, w, h):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print('(install Pillow for the contact sheet; the screenshots are in test/places/runs/)')
        return
    tw, th = 400, 400 * h // w
    sheet = Image.new('RGB', (4 * (tw + 4), len(rows) * (th + 18)), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for r, (label, check, paths) in enumerate(rows):
        y = r * (th + 18)
        d.text((4, y + 2), f'{label}: {check}', fill=(255, 220, 90))
        for c, path in enumerate(paths):
            if os.path.exists(path):
                sheet.paste(Image.open(path).convert('RGB').resize((tw, th)), (c * (tw + 4), y + 16))
    out = os.path.join(place_dir('sheets'), 'sheet.png')
    try:
        sheet.save(out)
    except OSError:
        # Open in an image viewer that locks it (Windows): next to it instead.
        k = 2
        while os.path.exists(os.path.join(place_dir('sheets'), f'sheet-{k}.png')):
            k += 1
        out = os.path.join(place_dir('sheets'), f'sheet-{k}.png')
        sheet.save(out)
    print(f'contact sheet: {out}')


def cmd_lockstep(a):
    failed = 0
    for p in selected(a.only):
        for t in times(a.time):
            name = snap_name(p, t)
            snap = os.path.join(place_dir('snapshots'), name + '.st')
            if not os.path.exists(snap):
                print(f'{name}: no snapshot (run "snapshots --time {t}" first)')
                continue
            start = snapshot_frame(snap)
            lines, end = walk_input(p, start)
            inp = os.path.join(place_dir('work'), 'walk.txt')
            write_input(inp, lines)
            log = os.path.join(place_dir('lockstep'), name + '.log')
            run_game([a.rom, '--headless', '--lockstep', '--no-save', '--save', os.path.join(place_dir('saves'), p['name'] + '.sav'),
                      '--state-load', snap, '--input', inp, '--frames', str(max(end, start + a.frames))], log, None)
            result = [l for l in open(log, errors='replace') if l.startswith('[lockstep] PASS') or 'differ' in l or 'FAIL' in l]
            ok = any(l.startswith('[lockstep] PASS') for l in result)
            failed += not ok
            print(f'{name}: {"PASS" if ok else "FAIL (see " + log + ")"}')
    sys.exit(1 if failed else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('save')
    s.add_argument('src')
    s.add_argument('dst')
    s.add_argument('place')
    s = sub.add_parser('snapshots')
    s.add_argument('rom')
    s.add_argument('save')
    s.add_argument('--only')
    s.add_argument('--time', default='day', help='times of day, e.g. day,night')
    s = sub.add_parser('run')
    s.add_argument('rom')
    s.add_argument('--only')
    s.add_argument('--time', default='day', help='times of day, e.g. day,night')
    s.add_argument('--set', action='append', default=[], help='a settings line, e.g. zoom=60')
    s.add_argument('--frames', type=int, default=240)
    s.add_argument('--size', default='1280x720')
    s = sub.add_parser('lockstep')
    s.add_argument('rom')
    s.add_argument('--only')
    s.add_argument('--time', default='day', help='times of day, e.g. day,night')
    s.add_argument('--frames', type=int, default=600)
    a = ap.parse_args()
    if a.cmd == 'save':
        make_save(a.src, a.dst, parse_place(a.place))
    elif a.cmd == 'snapshots':
        cmd_snapshots(a)
    elif a.cmd == 'run':
        cmd_run(a)
    else:
        cmd_lockstep(a)


if __name__ == '__main__':
    main()
