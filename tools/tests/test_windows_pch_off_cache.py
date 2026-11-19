"""Exercise the isolated Windows cache trial's dispatch and retention contract."""
# SPDX-License-Identifier: GPL-2.0-or-later

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]


class WindowsPchOffCacheTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        cls.steps = {step.get('name'): step for step in workflow['jobs']['build']['steps']}
        if os.name == 'nt':
            git = shutil.which('git.exe')
            if git is None:
                raise RuntimeError('Git for Windows is required by the Bash workflow')
            # Native subprocess lookup can find the System32 WSL stub before
            # PATH. GitHub Actions uses Git for Windows Bash for this workflow.
            candidates = [parent / 'bin/bash.exe' for parent in Path(git).resolve().parents]
            cls.bash = next((str(path) for path in candidates if path.is_file()), None)
            if cls.bash is None:
                raise RuntimeError('Could not locate Git for Windows Bash beside git.exe')
        else:
            cls.bash = 'bash'

    def run_setup(self, seed, full_build='true', populated=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            if populated:
                cache = root / '.sccache-pch-off-trial'
                cache.mkdir()
                (cache / 'existing-object').touch()
            step = self.steps['Select isolated PCH-off cache']
            env = dict(os.environ, CACHE_SEED=seed, FULL_PCH_OFF_BUILD=full_build,
                       GITHUB_RUN_ID='100', GITHUB_SHA='a' * 40, GITHUB_WORKSPACE=root.as_posix(),
                       GITHUB_ENV=(root / 'env.txt').as_posix(), GITHUB_OUTPUT=(root / 'output.txt').as_posix())
            result = subprocess.run([self.bash, '-c', step['run']], cwd=root, env=env,
                                    text=True, capture_output=True, check=False)
            return result, (root / 'output.txt').read_text() if (root / 'output.txt').exists() else ''

    def test_cold_and_warm_use_the_same_key_for_the_same_commit(self):
        cold, cold_output = self.run_setup('cold')
        warm, warm_output = self.run_setup('100')
        self.assertEqual(cold.returncode, 0, cold.stdout + cold.stderr)
        self.assertEqual(warm.returncode, 0, warm.stdout + warm.stderr)
        self.assertEqual(cold_output, 'key=sccache-new-horizons-msvc-x64-v142-pch-off-' + 'a' * 40 + '-100\n')
        self.assertEqual(cold_output, warm_output)

    def test_invalid_or_nonempty_trial_does_not_emit_a_cache_key(self):
        for seed, full_build, populated in [('a/100', 'true', False), ('cold', 'false', False),
                                            ('cold', 'true', True)]:
            with self.subTest(seed=seed, full_build=full_build, populated=populated):
                result, output = self.run_setup(seed, full_build, populated)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(output, '')

    def test_only_a_successful_cold_package_is_saved_as_a_warm_seed(self):
        names = list(self.steps)
        restore = self.steps['Restore the cold run\'s compiler cache']
        check = self.steps['Check warm compiler cache identity']
        save = self.steps['Save cold trial compiler cache']
        self.assertIs(restore['with']['fail-on-cache-miss'], True)
        self.assertNotIn('restore-keys', restore['with'])
        self.assertIn('"$REQUESTED_KEY" != "$MATCHED_KEY"', check['run'])
        self.assertEqual(save['with'], {k: restore['with'][k] for k in ('path', 'key')})
        self.assertEqual(save['if'], "inputs.pch_off_cache_seed == 'cold'")
        self.assertLess(names.index('Restore the cold run\'s compiler cache'),
                        names.index('Cache compiler outputs for packaging retries'))
        self.assertLess(names.index('Upload downloadable Windows preview'),
                        names.index('Save cold trial compiler cache'))
        diagnostics = self.steps['Retain Windows compiler diagnostics']
        self.assertIn('always()', diagnostics['if'])
        self.assertIs(diagnostics['with']['include-hidden-files'], True)
        self.assertIn('build/new-horizons-windows-x64/.ninja_log', diagnostics['with']['path'].splitlines())

    def test_normal_cache_restores_but_saves_only_without_pch_or_a_trial(self):
        cache = self.steps['Cache compiler outputs for packaging retries']
        self.assertNotIn('inputs.use_precompiled_headers', cache.get('if', ''))
        self.assertEqual(cache['with']['variant'], 'sccache')
        self.assertEqual(cache['with']['restore'], "${{ inputs.pch_off_cache_seed == '' }}")
        self.assertIsInstance(cache['with']['save'], str)
        self.assertEqual(cache['with']['save'],
                         "${{ !inputs.use_precompiled_headers && inputs.pch_off_cache_seed == '' }}")


if __name__ == '__main__':
    unittest.main()
