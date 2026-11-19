"""Offline ownership/witness/recovery checks, not native Windows acceptance."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
from pathlib import Path, PureWindowsPath
import shlex
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
import windows_nhart_interruption as gate

DIGEST = 'a' * 64
DATA = b'Actual payload bytes from the verified archive'


class OwnedProcess:
    def __init__(self, code=None):
        self.pid, self.returncode, self.killed = 123, code, False

    def poll(self):
        return self.returncode

    def kill(self):
        self.killed, self.returncode = True, 1

    def wait(self, timeout):
        if self.returncode is None:
            raise subprocess.TimeoutExpired('owned probe', timeout)
        return self.returncode


class WindowsNhartInterruptionTest(unittest.TestCase):
    def fixture(self, root):
        probe, pack = root / 'probe.exe', root / 'pack.nhart'
        probe.write_bytes(b'compiled probe')
        (root / 'VCMI_lib.dll').write_bytes(b'the packaged native library')
        pack.write_bytes(DATA)
        return probe, pack

    def run_fake(self, root, *, early_exit=False, bad_prefix=False, recovery_code=0, no_stage=False,
                 deep_cache=False):
        probe, pack = self.fixture(root)
        processes, commands = [], []
        def launch(command, **kwargs):
            self.assertIs(kwargs['shell'], False)
            self.assertEqual(kwargs['stdin'], subprocess.DEVNULL)
            commands.append(command)
            child = command[-1]
            self.assertRegex(child, r'^nhart-runtime-[0-9a-f]{4}(?:-[0-9a-f]{4}){3}$')
            owned = gate.filesystem_path(Path(command[command.index('--cache-root') + 1])) / child
            staging = owned / 'new-horizons-art/v1.staging'
            stage = staging / (DIGEST + '.stage-0123-4567-89ab-cdef')
            if command[-2] == '--create-owned-cache-child':
                process = OwnedProcess(0 if early_exit else None)
                if not no_stage:
                    stage.mkdir(parents=True)
                    (stage / 'payload-0').write_bytes(b'Wrong' if bad_prefix else DATA)
            else:
                self.assertEqual(command[-2], '--recover-owned-cache-child')
                self.assertEqual(command[-1], commands[0][-1])
                process = OwnedProcess(recovery_code)
                if not recovery_code:
                    (stage / 'payload-0').unlink()
                    stage.rmdir()
                    published = owned / 'new-horizons-art/v1' / DIGEST
                    published.mkdir(parents=True)
                    (published / 'payload-0').write_bytes(DATA)
                    (published / 'payload-1').write_bytes(b'manifest')
                    kwargs['stdout'].write(b'PASS: SHA256 vectors; all native resource parity\n')
            processes.append(process)
            return process
        clock = Mock(side_effect=[0, 100]) if no_stage else Mock(return_value=0)
        report = gate.run_check(probe, pack, root / 'output', (DIGEST, 1),
                                [{'offset': 0, 'size': len(DATA)}], popen=launch, clock=clock,
                                deep_cache=deep_cache)
        self.assertEqual(report, json.loads((root / 'output/report.json').read_text()))
        return report, processes, commands

    def test_kills_only_owned_process_after_matching_real_payload_and_reuses_exact_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            report, processes, commands = self.run_fake(Path(temporary))
            self.assertTrue(report['pass'])
            self.assertTrue(report['partialStageSurvivedKill'])
            self.assertTrue(processes[0].killed)
            self.assertFalse(processes[1].killed)
            self.assertEqual(report['witness']['matchedPrefixBytes'], len(DATA))
            self.assertEqual(commands[0][:-2], commands[1][:-2])
            self.assertEqual(report['cacheMode'], 'short')
            self.assertEqual(Path(commands[0][commands[0].index('--cache-root') + 1]),
                             Path(temporary) / 'output/cache')
            self.assertNotIn(str(temporary), json.dumps(report))

    def test_deep_cache_root_exceeds_limit_without_changing_short_log_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            report, processes, commands = self.run_fake(Path(temporary), deep_cache=True)
            self.assertTrue(report['pass'])
            self.assertTrue(processes[0].killed)
            self.assertFalse(processes[1].killed)
            self.assertEqual(report['cacheMode'], 'deep')
            self.assertGreater(report['cacheRootUTF16Units'], 260)
            self.assertGreater(report['witness']['stageUTF16Units'], 260)
            self.assertGreater(report['witness']['payloadUTF16Units'], 260)
            actual_cache = commands[0][commands[0].index('--cache-root') + 1]
            self.assertFalse(actual_cache.startswith('\\\\?\\'))
            self.assertEqual(gate.utf16_units(actual_cache), report['cacheRootUTF16Units'])
            self.assertEqual(commands[0][:-2], commands[1][:-2])
            self.assertTrue(report['partialStageSurvivedKill'])
            self.assertEqual(report['witness']['matchedPrefixBytes'], len(DATA))
            for name in ('report.json', 'killed.stdout.log', 'recovered.stdout.log'):
                self.assertTrue((Path(temporary) / 'output' / name).is_file())
                self.assertLess(gate.utf16_units(Path(temporary) / 'output' / name), 260)
            self.assertNotIn(str(temporary), json.dumps(report))

    def test_deep_mode_does_not_waive_failed_witness_exit_or_recovery(self):
        for options in ({'early_exit': True}, {'bad_prefix': True}, {'recovery_code': 9}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temporary:
                report, _, _ = self.run_fake(Path(temporary), deep_cache=True, **options)
                self.assertFalse(report['pass'])
                self.assertGreater(report['cacheRootUTF16Units'], 260)

    def test_windows_namespaces_measure_actual_utf16_not_extended_prefix(self):
        root = gate.cache_root(PureWindowsPath('D:/a/new-horizons/nhart-deep-io'), True)
        self.assertGreater(gate.utf16_units(root), 260)
        self.assertTrue(all(len(part) < 255 for part in root.parts))
        extended = gate.extended_windows_name(root)
        self.assertTrue(extended.startswith('\\\\?\\D:\\'))
        self.assertEqual(gate.normal_windows_name(extended), str(root))
        self.assertEqual(gate.utf16_units(extended), gate.utf16_units(root))
        self.assertEqual(gate.extended_windows_name(extended), extended)
        self.assertEqual(gate.extended_windows_name('\\\\server\\share\\cache'),
                         '\\\\?\\UNC\\server\\share\\cache')
        self.assertEqual(gate.utf16_units('D:\\orb-\U0001f535'), 9)
        for invalid in ('relative/cache', 'D:relative', 'D:\\cache\\..\\other', '\\\\.\\C:\\cache'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                gate.extended_windows_name(invalid)

    def test_prefixed_cache_cannot_claim_ordinary_native_argv_or_launch(self):
        with patch.object(gate.sys, 'platform', 'win32'):
            for ordinary in ('D:\\cache', '\\\\server\\share\\cache'):
                self.assertEqual(gate.native_cache_argument(PureWindowsPath(ordinary)), ordinary)
            for invalid in ('\\\\?\\D:\\cache', '\\\\?\\UNC\\server\\share\\cache', '\\\\.\\C:\\cache',
                            'relative', 'D:relative', 'D:\\cache\\..\\other'):
                with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                    gate.native_cache_argument(PureWindowsPath(invalid))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            probe, pack = self.fixture(root)
            launch = Mock()
            with patch.object(gate.sys, 'platform', 'win32'), \
                    patch.object(gate, 'cache_root', return_value=PureWindowsPath('\\\\?\\D:\\cache')):
                with self.assertRaisesRegex(ValueError, 'ordinary absolute'):
                    gate.run_check(probe, pack, root / 'output', (DIGEST, 1), [], popen=launch, deep_cache=True)
            launch.assert_not_called()
            self.assertFalse((root / 'output').exists())

    def test_normal_exit_without_live_witness_never_counts_as_interruption(self):
        with tempfile.TemporaryDirectory() as temporary:
            report, processes, commands = self.run_fake(Path(temporary), early_exit=True)
            self.assertFalse(report['pass'])
            self.assertFalse(processes[0].killed)
            self.assertEqual(len(commands), 1)

    def test_witness_timeout_fails_and_reaps_only_the_owned_child(self):
        with tempfile.TemporaryDirectory() as temporary:
            report, processes, commands = self.run_fake(Path(temporary), no_stage=True)
            self.assertFalse(report['pass'])
            self.assertEqual(len(commands), 1)
            self.assertTrue(processes[0].killed)
            self.assertEqual(report['cleanupExitCode'], 1)

    def test_wrong_payload_bytes_or_failed_native_recovery_cannot_pass(self):
        for options in ({'bad_prefix': True}, {'recovery_code': 9}, {'recovery_code': None}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temporary:
                report, processes, _ = self.run_fake(Path(temporary), **options)
                self.assertFalse(report['pass'])
                if options.get('recovery_code', 0) is None:
                    self.assertTrue(processes[-1].killed)

    def test_existing_output_is_never_reused_or_cleaned(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            probe, pack = self.fixture(root)
            output = root / 'existing'
            output.mkdir()
            sentinel = output / 'caller-data'
            sentinel.write_bytes(b'preserve')
            launch = Mock()
            for deep in (False, True):
                with self.subTest(deep=deep), self.assertRaises(FileExistsError):
                    gate.run_check(probe, pack, output, (DIGEST, 1), [], popen=launch, deep_cache=deep)
            launch.assert_not_called()
            self.assertEqual(sentinel.read_bytes(), b'preserve')

    def test_windows_reparse_attributes_are_rejected(self):
        path = Mock()
        path.lstat.return_value = SimpleNamespace(st_mode=0o40700, st_file_attributes=0x400)
        with self.assertRaisesRegex(ValueError, 'reparse'):
            gate.plain(path, directory=True)

    def test_workflow_runs_same_native_gate_after_compile_and_before_packaging(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        steps = workflow['jobs']['build']['steps']
        names = [row.get('name') for row in steps]
        native = next(row for row in steps if row.get('name') == 'Verify native NHART preparation and matched-byte reads (CPU only)')
        self.assertEqual(shlex.split(native['run'])[:3],
                         ['bash', 'tools/ci/run_windows_nhart_native.sh', '$NH_WINDOWS_PYTHON'])
        self.assertEqual(native['env']['NH_WINDOWS_PYTHON'], '${{ steps.python.outputs.python-path }}')
        helper = (ROOT / 'tools/ci/run_windows_nhart_native.sh').read_text()
        self.assertEqual(helper.count('"$python_exe" tools/ci/windows_nhart_interruption.py --repo . --probe "$test_exe"'), 2)
        self.assertIn('set -euo pipefail', helper)
        self.assertIn('python_exe=$(cygpath -u "$1")', helper)
        self.assertIn('--output "$build_root/nhart-io"', helper)
        self.assertIn('--output "$build_root/nhart-deep-io" --deep-cache', helper)
        self.assertLess(helper.index('--output "$build_root/nhart-io"'),
                        helper.index('--output "$build_root/nhart-deep-io"'))
        self.assertNotIn('inputs.use_unity_build', native['if'])
        self.assertLess(names.index('Compile Windows x64 client'), names.index(native['name']))
        self.assertLess(names.index(native['name']), names.index('Package client, recursive PE closure, licenses and corresponding sources'))
        artifact = next(row for row in steps if row.get('name') == 'Retain native NHART interruption recovery evidence')
        self.assertIn('always()', artifact['if'])
        self.assertNotIn('/cache', artifact['with']['path'])
        source = (ROOT / 'client/tests/NhArtStartupRuntimeTest.cpp').read_text()
        self.assertIn('(argc != 9 && argc != 11)', source)
        self.assertIn('--create-owned-cache-child', source)
        self.assertIn('--recover-owned-cache-child', source)


    def test_workflow_interruption_cache_fits_classic_windows_paths(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        steps = workflow['jobs']['build']['steps']
        native = next(row for row in steps if row.get('name') ==
                      'Verify native NHART preparation and matched-byte reads (CPU only)')
        build_root = shlex.split(native['run'])[3]
        output = build_root + '/nhart-io'
        workspace = PureWindowsPath('D:/a/new-horizons/new-horizons')
        cache = workspace / output / 'cache' / 'nhart-runtime-aaaa-bbbb-cccc-dddd'
        stage = cache / 'new-horizons-art' / 'v1.staging' / (DIGEST + '.stage-aaaa-bbbb-cccc-dddd')
        # CreateDirectoryW needs room for an 8.3 name. Also check the deepest
        # stage file, allowing ten digits for the art record index.
        self.assertLessEqual(len(str(stage).encode('utf-16-le')) // 2, 247)
        self.assertLessEqual(len(str(stage / 'payload-9999999999').encode('utf-16-le')) // 2, 259)
        deep = gate.cache_root(workspace / build_root / 'nhart-deep-io', True)
        self.assertGreater(gate.utf16_units(deep), 260)
        self.assertLess(gate.utf16_units(workspace / build_root / 'nhart-deep-io/report.json'), 260)
        artifact = next(row for row in steps if row.get('name') ==
                        'Retain native NHART interruption recovery evidence')
        self.assertEqual(set(artifact['with']['path'].splitlines()),
                         {output + '/*.json', output + '/*.log',
                          build_root + '/nhart-deep-io/*.json', build_root + '/nhart-deep-io/*.log'})

if __name__ == '__main__':
    unittest.main()
