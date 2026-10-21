#!/usr/bin/env python3
"""ROM-free generator, embedded-data link, and full native frontend build smoke test.

Usage: python3 tests/native_smoke.py --build-dir '/tmp/ssrecomp smoke'
The fixture is synthetic ARM/Thumb instructions, never commercial game content.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import recomp


def run(cmd):
    print('+', shlex.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True)


def fixture(dest):
    # Include both ARM and Thumb emitters, multiple symbols, and non-aligned sizes.
    arm = recomp.Section('synthetic_arm', 0x02000000, bytes.fromhex('0100a0e3feffffea5a'))
    thumb = recomp.Section('synthetic_thumb', 0x02001000, bytes.fromhex('0120fee75b5c'))
    arm.ranges = [(arm.base, arm.base + 8, False)]
    thumb.ranges = [(thumb.base, thumb.base + 4, True)]
    for sec in (arm, thumb):
        sec.named = {}
    with patch.object(recomp, 'load_sections', return_value=[arm, thumb]), patch.object(
            sys, 'argv', ['recomp.py', 'synthetic', str(ROOT / 'melonDS/src'), str(dest)]):
        recomp.main()
    (dest / 'apps_table.cpp').write_text(
        '#include "recomp_api.h"\n'
        'const RcApp rc_apps[] = {{nullptr, nullptr, 0}};\nconst unsigned rc_num_apps = 0;\n'
        'const RcApp rc_focus_syms[] = {{nullptr, nullptr, 0}};\nconst unsigned rc_num_focus_syms = 0;\n')
    # Link against C++ declarations, verify every byte and 16-byte alignment of both images.
    checks = ['#include <cstdint>', 'extern "C" {']
    checks += [f'extern const unsigned char rc_image_{i}[];' for i in range(2)]
    checks += ['}', 'int main() {']
    for i, sec in enumerate((arm, thumb)):
        checks += [f'if (reinterpret_cast<std::uintptr_t>(rc_image_{i}) % 16) return 1;']
        checks += [f'if (rc_image_{i}[{j}] != {b}) return 2;' for j, b in enumerate(sec.image)]
    checks += ['return 0; }']
    (dest / 'image_check.cpp').write_text('\n'.join(checks))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, required=True)
    ap.add_argument('--fixture-only', action='store_true')
    args = ap.parse_args()
    build = args.build_dir.resolve()
    gen = build / 'generated fixtures with spaces'
    gen.mkdir(parents=True, exist_ok=True)
    fixture(gen)
    if args.fixture_only:
        return
    cxx = shlex.split(os.environ.get('CXX', 'c++'))
    check = build / ('image_check.exe' if os.name == 'nt' else 'image_check')
    run([*cxx, '-std=c++17', gen / 'image_check.cpp', gen / 'rc_images.S',
         '-Wa,-I' + str(gen), '-o', check])
    run([check])
    run(['cmake', '-S', ROOT, '-B', build / 'native', '-G', 'Ninja',
         '-DSSR_GENERATED_DIR=' + str(gen), '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
         '-DSSR_PORTABLE_X86=ON'])
    run(['cmake', '--build', build / 'native', '--parallel', '2'])
    # --help returns before a ROM is loaded: exercises the linked frontend entry point.
    result = subprocess.run([str(build / 'native' / ('johto-online.exe' if os.name == 'nt' else 'johto-online')),
                             '--help'], capture_output=True, text=True)
    assert result.returncode == 2 and 'usage: johto-online' in result.stderr, result
    print('Native generator/link/frontend smoke test passed.', flush=True)


if __name__ == '__main__':
    main()
