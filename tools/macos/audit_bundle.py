#!/usr/bin/env python3
"""Verify the setup bundle is ad-hoc/Developer ID signed, relocatable and game-free."""
import argparse
from pathlib import Path
import plistlib
import subprocess

from check_binary import check_binary


def audit(bundle):
    info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
    assert info['CFBundleIdentifier'] == 'io.github.johtoonline.setup'
    assert info['NSHighResolutionCapable']
    minimum = info['LSMinimumSystemVersion']
    source_dirs = list(bundle.rglob('project'))
    assert source_dirs, 'Source payload missing'
    assert list(bundle.rglob('bundle.id')), 'Source version stamp missing'
    binaries = 0
    for path in bundle.rglob('*'):
        if path.is_symlink() or not path.is_file():
            continue
        assert path.suffix not in {'.nds', '.sav', '.state', '.nef', '.sbin'}, path
        assert path.name not in {'.DS_Store', 'johto-online.rom', 'johto-online.cfg'}, path
        assert not path.name.startswith('rc_image_'), path
        with path.open('rb') as f:
            magic = f.read(4)
        if magic not in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
            continue
        binaries += 1
        check_binary(path, minimum)
        deps = subprocess.check_output(['otool', '-L', str(path)], text=True)
        for line in deps.splitlines()[1:]:
            dep = line.strip().split(' (compatibility version')[0]
            assert not dep.startswith('/') or dep.startswith(('/usr/lib/', '/System/Library/')), \
                f'Non-relocatable dylib in {path}: {dep}'
    assert binaries, 'No Mach-O executable found'
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
    print(f'Bundle audit passed ({binaries} Mach-O files).')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('bundle', type=Path)
    audit(ap.parse_args().bundle.resolve())
