"""Windows diagnostic collection retains failed-build evidence and exact compile contracts."""
# SPDX-License-Identifier: GPL-2.0-or-later

from contextlib import redirect_stderr
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
import build_windows_client as build
import windows_build_diagnostics as diagnostics


def stats(requests=0, hits=0, misses=0, bypass=0):
    return {'version': '0.10.0', 'stats': {
        'compile_requests': requests, 'requests_not_cacheable': bypass,
        'cache_hits': {'counts': {'C/C++': hits}, 'adv_counts': {'C++ [msvc]': hits}},
        'cache_misses': {'counts': {'C/C++': misses}, 'adv_counts': {'C++ [msvc]': misses}},
        'cache_errors': {'counts': {}, 'adv_counts': {}},
        'not_cached': {'/Fp': bypass},
    }}


class WindowsBuildDiagnosticsTest(unittest.TestCase):
    def test_delta_uses_v010_nested_per_language_counts_and_detects_resets(self):
        result = diagnostics.cache_delta(stats(10, 2, 3, 5), stats(40, 22, 11, 7))
        self.assertEqual(result['compile_requests'], 30)
        self.assertEqual(result['cache_hits'], {'C/C++': 20})
        self.assertEqual(result['cache_hits_by_compiler'], {'C++ [msvc]': 20})
        self.assertEqual(result['cache_misses'], {'C/C++': 8})
        self.assertEqual(result['not_cached'], {'/Fp': 2})
        self.assertFalse(result['counters_reset'])
        self.assertTrue(diagnostics.cache_delta(stats(10, 8), stats(1))['counters_reset'])
        self.assertEqual(diagnostics.cache_delta({}, stats()), {'available': False})

    def test_ninja_log_lists_new_slow_actions_without_summing_parallel_spans(self):
        previous = '# ninja log v5\n0\t5\t1\tbootstrap.obj\taaaa\n'
        current = previous + '5\t500\t2\tclient.pch\tbbbb\n5\t900\t3\tclient.exe\tcccc\npartial\n'
        report = diagnostics.ninja_actions(current, previous)
        self.assertEqual(report['newCompletedOutputCount'], 2)
        self.assertEqual(report['invalidRows'], 1)
        self.assertEqual(report['longestCompletedOutputs'][0]['output'], 'client.exe')
        self.assertEqual(report['longestCompletedOutputs'][0]['durationMilliseconds'], 895)
        self.assertNotIn('total', report)
        self.assertFalse(diagnostics.ninja_actions('# ninja log v4\n', '')['available'])
        self.assertEqual(diagnostics.ninja_actions(current, '')['newCompletedOutputCount'], 3)

    def test_ninja_v6_and_v7_keep_same_five_field_action_timing(self):
        for version in (6, 7):
            with self.subTest(version=version):
                content = f'# ninja log v{version}\n10\t620000\t1743773265703\tclient.pch\t8b815c2e9722af74\n'
                report = diagnostics.ninja_actions(content, '')
                self.assertTrue(report['available'])
                self.assertEqual(report['version'], version)
                self.assertEqual(report['newCompletedOutputCount'], 1)
                self.assertEqual(report['longestCompletedOutputs'][0]['durationMilliseconds'], 619990)
                self.assertEqual(report['longestCompletedOutputs'][0]['output'], 'client.pch')

    def test_failed_guard_retains_cache_snapshots_ninja_log_and_original_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tree, output = root / 'build tree', root / 'diagnostics'
            tree.mkdir()
            (tree / 'CMakeCache.txt').write_text(
                'ENABLE_PCH:BOOL=OFF\nCMAKE_BUILD_TYPE:STRING=RelWithDebInfo\n'
                'CMAKE_CXX_COMPILER:FILEPATH=C:/toolchain/bin/cl.exe\nSECRET:TOKEN=omit\n')
            (tree / '.ninja_log').write_text('# ninja log v7\n0\t5\t1\tbootstrap.obj\taaaa\n')
            snapshots = iter((stats(2, 0, 2), stats(5, 1, 3, 1)))

            def run(command, **kwargs):
                if command[0] == 'sccache':
                    content = json.dumps(next(snapshots)) if '--stats-format=json' in command else 'Cache statistics\n'
                    self.assertEqual(kwargs['timeout'], 30)
                    return SimpleNamespace(returncode=0, stdout=content, stderr='')
                with (tree / '.ninja_log').open('a') as stream:
                    stream.write('5\t200\t2\tguard.obj\tbbbb\n')
                return SimpleNamespace(returncode=17)

            with patch.object(build.subprocess, 'run', side_effect=run) as calls:
                status = build.main(['build', '--build-root', str(tree), '--diagnostics-dir', str(output),
                                     '--cache-key-requested', 'wanted', '--cache-key-matched', 'actual'])
            self.assertEqual(status, 17)
            compile_calls = [invocation for invocation in calls.call_args_list if invocation.args[0][0] != 'sccache']
            self.assertEqual(len(compile_calls), 1)
            self.assertEqual(compile_calls[0].args[0], [
                'cmake', '--build', str(tree), '--target', *build.GUARD_OBJECT_TARGETS,
                '--parallel', '4', '--', '-d', 'keeprsp', '-k', '0'])
            report = json.loads((output / 'build-diagnostics.json').read_text())
            self.assertEqual(report['stages'][0]['exitCode'], 17)
            self.assertGreaterEqual(report['wallSeconds'], 0)
            self.assertEqual(report['cacheKeyMatched'], 'actual')
            self.assertEqual(report['cacheDelta']['cache_hits'], {'C/C++': 1})
            self.assertEqual(report['sccacheVersion'], '0.10.0')
            self.assertEqual(report['ninja']['version'], 7)
            self.assertEqual(report['settings']['ENABLE_PCH'], 'OFF')
            self.assertEqual(report['settings']['CMAKE_CXX_COMPILER'], 'cl.exe')
            self.assertNotIn('SECRET', report['settings'])
            self.assertNotIn(str(tree), json.dumps(report))
            self.assertEqual((output / '.ninja_log').read_bytes(), (tree / '.ninja_log').read_bytes())
            for name in ('sccache-before.json', 'sccache-before.txt', 'sccache-after.json', 'sccache-after.txt'):
                self.assertTrue((output / name).is_file())

    def test_missing_sccache_and_missing_ninja_do_not_change_successful_build_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)

            def run(command, **kwargs):
                if command[0] == 'sccache':
                    raise FileNotFoundError('sccache unavailable')
                return SimpleNamespace(returncode=0)

            with patch.object(build.subprocess, 'run', side_effect=run):
                status = build.main(['build', '--build-root', str(root / 'absent'),
                                     '--diagnostics-dir', str(root / 'diagnostics')])
            self.assertEqual(status, 0)
            report = json.loads((root / 'diagnostics/build-diagnostics.json').read_text())
            self.assertEqual(len(report['diagnosticErrors']), 4)
            self.assertEqual(len(report['stages']), 2)
            self.assertFalse(report['cacheDelta']['available'])
            self.assertFalse(report['ninja']['available'])

    def test_snapshot_failure_and_finish_failure_preserve_compile_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            completed = SimpleNamespace(returncode=0, stdout='not json', stderr='diagnostic stderr')
            results = [completed, completed, SimpleNamespace(returncode=31), completed, completed]
            with patch.object(build.subprocess, 'run', side_effect=results):
                self.assertEqual(build.main(['build', '--build-root', str(root),
                                             '--diagnostics-dir', str(root / 'evidence')]), 31)
            report = json.loads((root / 'evidence/build-diagnostics.json').read_text())
            self.assertEqual(len(report['diagnosticErrors']), 2)
            with patch.object(diagnostics.BuildDiagnostics, '__init__', return_value=None), \
                    patch.object(diagnostics.BuildDiagnostics, 'run', return_value=37), \
                    patch.object(diagnostics.BuildDiagnostics, 'finish', side_effect=OSError('read-only disk')), \
                    redirect_stderr(io.StringIO()) as errors:
                self.assertEqual(build.main(['build', '--build-root', 'tree', '--diagnostics-dir', 'evidence']), 37)
            self.assertIn('Could not finish Windows build diagnostics', errors.getvalue())


if __name__ == '__main__':
    unittest.main()
