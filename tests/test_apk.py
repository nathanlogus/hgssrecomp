"""tools/android/apk.py: the player's own files and their index in the APK."""
from pathlib import Path
import json
import os
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'android'))
import apk


def touch(path, data=b'x'):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


class ApkUserFilesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        a = self.dir / 'assets'
        for name in ('fonts/B-Bold.otf', 'fonts/a.TTF', 'fonts/notes.txt', 'buttons/icon_key_a_00.png',
                     'songs/songs.txt', 'songs/1 Town.mp3', 'songs/.hidden.mp3', 'animations/intro.mpg',
                     'animations/intro.mp4', 'autosave.gif'):
            touch(a / name)
        self.assets = str(a)

    def tearDown(self):
        self.tmp.cleanup()

    def test_media_on_and_off(self):
        names = [n for n, _ in apk.user_files(self.assets)]
        self.assertEqual(names, ['fonts/B-Bold.otf', 'fonts/a.TTF', 'buttons/icon_key_a_00.png',
                                 'songs/1 Town.mp3', 'songs/songs.txt', 'autosave.gif', 'animations/intro.mpg'])
        small = [n for n, _ in apk.user_files(self.assets, media=False)]
        self.assertEqual(small, ['fonts/B-Bold.otf', 'fonts/a.TTF', 'buttons/icon_key_a_00.png', 'autosave.gif'])
        self.assertEqual(apk.user_files(str(self.dir / 'none')), [])

    def test_build_packs_files_and_index(self):
        skeleton, lib, rom, key, out = (str(self.dir / n) for n in ('s.apk', 'lib.so', 'rom.nds', 'key.json', 'o.apk'))
        with zipfile.ZipFile(skeleton, 'w') as z:
            z.writestr('AndroidManifest.xml', b'manifest')
        touch(Path(lib))
        touch(Path(rom), b'rom')
        # A small key keeps the test fast; real builds make a 2048-bit one.
        k = apk.new_key(512)
        Path(key).write_text(json.dumps({'n': '%x' % k['n'], 'e': k['e'], 'd': '%x' % k['d']}))
        files = apk.user_files(self.assets, media=False)
        with patch.object(apk, 'user_files', lambda _a, media: files if not media else self.fail('media')):
            apk.build(skeleton, 'arm64-v8a', lib, lib, rom, key, out, media=False)
        with zipfile.ZipFile(out) as z:
            names = z.namelist()
            index = z.read('assets/' + apk.USER_INDEX).decode().splitlines()
            self.assertEqual(z.getinfo('assets/fonts/B-Bold.otf').compress_type, zipfile.ZIP_DEFLATED)
            self.assertEqual(z.getinfo('assets/autosave.gif').compress_type, zipfile.ZIP_STORED)
        self.assertEqual(index, [n for n, _ in files])
        for n in index:
            self.assertIn('assets/' + n, names)
        self.assertIn(b'APK Sig Block 42', Path(out).read_bytes())
        self.assertFalse(os.path.exists(out + '.unsigned'))


if __name__ == '__main__':
    unittest.main()
