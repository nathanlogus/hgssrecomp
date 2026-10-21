"""ROM-free portability regressions. Run: python3 -m unittest discover -s tests"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import recomp
import johto_online_setup as setup


class DarwinSetupTests(unittest.TestCase):
    def test_memory_budget(self):
        with patch.object(setup, 'DARWIN', True), patch.object(setup, 'WINDOWS', False), \
             patch.object(os, 'cpu_count', return_value=16), \
             patch.object(subprocess, 'check_output', return_value=str(16 * 1024**3)):
            self.assertEqual(setup.default_jobs(), 4)
        with patch.object(setup, 'DARWIN', True), patch.object(setup, 'WINDOWS', False), \
             patch.object(os, 'cpu_count', return_value=16), \
             patch.object(subprocess, 'check_output', side_effect=OSError):
            self.assertEqual(setup.default_jobs(), 2)

    def test_finder_tool_search(self):
        with patch.object(setup, 'DARWIN', True), patch.object(setup, 'MAC_ARCH', 'arm64'), \
             patch.dict(os.environ, {'PATH': '/usr/bin:/bin'}, clear=True):
            setup.find_macos_tools()
            paths = os.environ['PATH'].split(os.pathsep)
            self.assertIn('/opt/homebrew/bin', paths)
            self.assertIn('/usr/local/bin', paths)
            self.assertEqual(os.environ['CXX'], 'clang++')

    def test_android_rejected_before_rom_or_download(self):
        with patch.object(setup, 'DARWIN', True):
            with self.assertRaisesRegex(RuntimeError, 'not supported'):
                setup.run_steps('no ROM supplied', 2, print, print, android=True)
            with self.assertRaisesRegex(RuntimeError, 'not supported'):
                setup.android_ndk(print, print)

    def test_compiler_architecture_error_is_actionable(self):
        setup.macos_toolchain_problem.cache_clear()
        result = subprocess.CompletedProcess([], 1, 'incompatible architecture: x86_64, expected arm64')
        with patch.object(setup, 'MAC_ARCH', 'arm64'), \
             patch.object(subprocess, 'run', return_value=result):
            self.assertIn('incompatible architecture', setup.macos_toolchain_problem('test', ''))
        setup.macos_toolchain_problem.cache_clear()

    def test_rebuild_reconfigures_and_honors_explicit_jobs(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp build with spaces ') as tmp:
            build = str(Path(tmp) / 'build-macos-arm64')
            Path(build).mkdir()
            (Path(build) / 'CMakeCache.txt').write_text('CMAKE_CACHEFILE_DIR:INTERNAL=/old/location\n')
            (Path(build) / 'CMakeFiles').mkdir()
            (Path(build) / 'ssrecomp').write_text('keep executable')
            proc = unittest.mock.Mock()
            proc.stdout = iter(())
            proc.wait.return_value = 0
            with patch.object(setup, 'HERE', tmp), patch.object(setup, 'BUILD', build), \
                 patch.object(setup, 'ROM_NOTE', str(Path(tmp) / 'rom path')), \
                 patch.object(setup, 'DARWIN', True), patch.object(setup, 'WINDOWS', False), \
                 patch.object(setup, 'MAC_ARCH', 'arm64'), \
                 patch('runpy.run_path'), patch.object(subprocess, 'Popen', return_value=proc) as popen, \
                 patch.object(subprocess, 'check_output', return_value='/opt/homebrew'):
                for _ in range(2):
                    setup.run_steps('/synthetic ROM.nds', 3, lambda t: None, lambda f, l: None)
            self.assertFalse((Path(build) / 'CMakeCache.txt').exists())
            self.assertFalse((Path(build) / 'CMakeFiles').exists())
            self.assertEqual((Path(build) / 'ssrecomp').read_text(), 'keep executable')
            commands = [c.args[0] for c in popen.call_args_list]
            configs = [c for c in commands if c[0] == 'cmake']
            self.assertEqual(len(configs), 2)
            self.assertIn('-DCMAKE_OSX_ARCHITECTURES=arm64', configs[0])
            self.assertIn(build, configs[0])
            self.assertEqual(commands[-1], ['ninja', '-C', build, '-j', '3'])

    def test_portable_launch_resolves_rom_and_replaces_log(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp portable ') as tmp:
            note = Path(tmp) / 'johto-online.rom'
            note.write_text('roms/game.nds\n')
            log = Path(tmp) / 'game.log'
            log.write_text('old launch\n')
            with patch.object(setup, 'HERE', tmp), patch.object(setup, 'ROM_NOTE', str(note)):
                for launch in ('first', 'second'):
                    def spawn(cmd, **kwargs):
                        self.assertEqual(cmd[1], os.path.join(tmp, 'roms', 'game.nds'))
                        self.assertIn('--config', cmd)
                        kwargs['stdout'].write(launch + '\n')
                    with patch.object(subprocess, 'Popen', side_effect=spawn):
                        setup.play()
                    self.assertEqual(log.read_text(), launch + '\n')

    def test_packaged_updates_preserve_user_files(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp update with spaces ') as tmp:
            data, payload = Path(tmp) / 'user data', Path(tmp) / 'payload'
            (payload / 'project/runtime').mkdir(parents=True)
            (payload / 'project/runtime/version.txt').write_text('new source')
            (payload / 'bundle.id').write_text('new version')
            data.mkdir()
            for name in ('soulsilver.sav', 'johto-online.cfg', 'johto-online.rom'):
                (data / name).write_text('keep me')
            with patch.object(setup, 'FROZEN', True), patch.object(setup, 'HERE', str(data)), \
                 patch.object(sys, '_MEIPASS', str(payload), create=True):
                setup.unpack()
                setup.unpack()
            self.assertEqual((data / 'runtime/version.txt').read_text(), 'new source')
            for name in ('soulsilver.sav', 'johto-online.cfg', 'johto-online.rom'):
                self.assertEqual((data / name).read_text(), 'keep me')


class AssemblyFormatTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('clang') and sys.platform != 'win32', 'cross assembler needs clang')
    def test_macho_elf_and_coff_assemble_same_generated_file(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp object formats ') as tmp:
            dest = Path(tmp)
            sections = [recomp.Section('one', 0, b'\x01\x02\xff'), recomp.Section('two', 16, b'\x04\x05')]
            recomp.write_images(tmp, sections)
            for target in ('arm64-apple-macos11', 'x86_64-apple-macos11',
                           'x86_64-unknown-linux-gnu', 'x86_64-w64-windows-gnu'):
                subprocess.run(['clang', '-target', target, '-c', str(dest / 'rc_images.S'),
                                '-Wa,-I' + tmp, '-o', str(dest / (target + '.o'))], check=True,
                               capture_output=True)


if __name__ == '__main__':
    unittest.main()
