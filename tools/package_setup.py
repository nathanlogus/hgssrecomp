#!/usr/bin/env python3
"""Packages johto_online_setup.py and the project's source into one executable with
PyInstaller (dist/johto-online-setup, or dist/johto-online-setup.exe on Windows). It holds no
game code: the game is still recompiled from the player's own ROM and compiled on
their machine. Run it on the system the executable is for.

macOS produces dist/johto-online-setup.app (Python/Tk included; local build tools required).

usage: pip install "pyinstaller>=6" certifi pillow && python tools/package_setup.py
"""
import hashlib
import json
import platform
import os
import shutil
import sys
import tempfile

import PyInstaller.__main__

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# What the build needs; build output, generated code and anything of the game stay out.
INCLUDE = ['CMakeLists.txt', 'LICENSE', 'README.md', 'CHANGELOG.md', 'run.sh', 'cmake', 'frontend', 'runtime', 'tools', 'layouts',
           'melonDS', 'assets']
SKIP_DIRS = {'.git', '.github', '__pycache__', 'build', 'gen', '.DS_Store'}

def skip_payload(_directory, names):
    return [n for n in names if n in SKIP_DIRS or n.startswith(('build-', 'gen.new'))
            or n.endswith(('.nds', '.sav', '.state', '.nef', '.sbin', '.pyc'))
            or n in {'johto-online.rom', 'johto-online.cfg', 'game.log', 'build.log'}]


with tempfile.TemporaryDirectory(prefix='ssrecomp-package-') as stage:
    project = os.path.join(stage, 'project')
    digest = hashlib.sha1()
    for item in INCLUDE:
        src = os.path.join(ROOT, item)
        dst = os.path.join(project, item)
        if os.path.isdir(src):
            shutil.copytree(src, dst, ignore=skip_payload)
        else:
            os.makedirs(project, exist_ok=True)
            shutil.copy2(src, dst)
    for base, dirs, files in sorted(os.walk(project)):
        dirs.sort()
        for f in sorted(files):
            digest.update(os.path.relpath(os.path.join(base, f), project).encode())
            digest.update(open(os.path.join(base, f), 'rb').read())
    with open(os.path.join(stage, 'bundle.id'), 'w') as f:
        f.write(digest.hexdigest())

    sep = os.pathsep
    hidden_imports = ['argparse', 'filecmp', 'collections', 'struct', 'runpy', 'io',
                      'secrets', 'zipfile', 'tarfile', 'urllib.request', 'ssl', 'certifi', 'tkinter']
    if sys.platform == 'darwin':
        os.environ['SSRECOMP_PACKAGE_STAGE'] = stage
        os.environ['SSRECOMP_PACKAGE_IMPORTS'] = json.dumps(hidden_imports)
        os.environ['SSRECOMP_PACKAGE_ARCH'] = platform.machine()
        PyInstaller.__main__.run([os.path.join(ROOT, 'tools', 'macos', 'setup.spec'), '--noconfirm',
                                 '--distpath', os.path.join(ROOT, 'dist'), '--workpath', os.path.join(stage, 'work')])
    else:
        PyInstaller.__main__.run([
            os.path.join(ROOT, 'johto_online_setup.py'),
            '--onefile', '--name', 'johto-online-setup', '--noconfirm',
            '--distpath', os.path.join(ROOT, 'dist'), '--workpath', os.path.join(stage, 'work'), '--specpath', stage,
            '--add-data', f'{project}{sep}project',
            '--add-data', f'{os.path.join(stage, "bundle.id")}{sep}.',
            *[arg for name in hidden_imports for arg in ('--hidden-import', name)],
            *(['--windowed'] if sys.platform == 'win32' else []),
        ])
print('->', os.path.join(ROOT, 'dist'))
