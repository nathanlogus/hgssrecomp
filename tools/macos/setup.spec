# Finder setup bundle. No ROM, generated code, or locally compiled game is included.
import json
import os
from pathlib import Path

root = Path(SPECPATH).resolve().parents[1]
stage = Path(os.environ['SSRECOMP_PACKAGE_STAGE'])
a = Analysis(
    [str(root / 'johto_online_setup.py')],
    pathex=[str(root)], binaries=[],
    datas=[(str(stage / 'project'), 'project'), (str(stage / 'bundle.id'), '.')],
    hiddenimports=json.loads(os.environ['SSRECOMP_PACKAGE_IMPORTS']),
    hookspath=[], runtime_hooks=[], excludes=[], noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name='johto-online-setup',
          debug=False, strip=False, upx=False, console=False,
          target_arch=os.environ.get('SSRECOMP_PACKAGE_ARCH'),
          codesign_identity=os.environ.get('SSRECOMP_CODESIGN_IDENTITY'))
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name='johto-online-setup')
app = BUNDLE(coll, name='johto-online-setup.app',
             icon=str(root / 'tools/appimage/johto-online-setup.png'),
             bundle_identifier='io.github.johtoonline.setup',
             info_plist={'CFBundleName': 'Johto Online setup',
                         'CFBundleShortVersionString': '0.1.0',
                         'LSMinimumSystemVersion': '11.0',
                         'NSHighResolutionCapable': True})
