"""Offline checks for the exact branch-only, matched Windows preview dispatch."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
BRANCH = 'refs/heads/performance/quick-combat-and-ci'
CONTROLS = {'preflight_only': False, 'use_precompiled_headers': True,
            'use_hot_header_pch': False, 'build_jobs': 4,
            'pch_off_cache_seed': '', 'repack_run_id': '', 'use_unity_build': False,
            'skip_pch_cache_save': False}


class WindowsPreviewDispatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        cls.job = cls.workflow['jobs']['build']
        cls.steps = {row.get('name'): row for row in cls.job['steps']}

    def script(self, name):
        text = self.steps[name]['run']
        self.assertTrue(text.startswith("python - <<'PY'\n"))
        self.assertTrue(text.endswith('PY\n'))
        return text.split('\n', 1)[1].rsplit('PY\n', 1)[0]

    def run_controls(self, controls, revision=None):
        with tempfile.TemporaryDirectory() as temporary:
            summary = Path(temporary) / 'summary'
            revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() if revision is None else revision
            env = dict(os.environ, TRIAL_INPUTS=json.dumps(controls), GITHUB_SHA=revision,
                       GITHUB_STEP_SUMMARY=str(summary))
            result = subprocess.run([sys.executable, '-B', '-c', self.script('Validate matched performance preview controls')],
                                    cwd=ROOT, env=env, text=True, capture_output=True, check=False)
            return result, summary.read_text() if summary.exists() else ''

    def test_only_main_and_exact_manual_branch_are_admitted(self):
        self.assertEqual(' '.join(self.job['if'].split()),
                         "github.repository == 'thegandalf196/new-horizons' && "
                         "(github.ref == 'refs/heads/main' || "
                         "(github.event_name == 'workflow_dispatch' && github.ref == '" + BRANCH + "'))")
        self.assertEqual(self.steps['Validate matched performance preview controls']['if'], "github.ref == '" + BRANCH + "'")
        self.assertEqual(self.workflow['permissions'], {'contents': 'read', 'actions': 'read'})
        self.assertEqual(self.workflow['concurrency'], {'group': 'new-horizons-windows-x64', 'cancel-in-progress': False})

    def test_both_arms_accept_identical_controls_and_record_source(self):
        for unity in (False, True):
            result, summary = self.run_controls(dict(CONTROLS, use_unity_build=unity))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Preview artifacts only', summary)
            self.assertIn(json.dumps(unity), summary)

    def test_actual_github_raw_inputs_accept_only_canonical_normalization(self):
        # Actual failed dispatch shape: a numeric input string and omitted empty strings.
        for unity in (False, True):
            raw = {'preflight_only': False, 'use_precompiled_headers': True,
                   'use_hot_header_pch': False, 'build_jobs': '4',
                   'skip_pch_cache_save': False, 'use_unity_build': unity}
            result, summary = self.run_controls(raw)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(json.dumps(dict(CONTROLS, use_unity_build=unity), sort_keys=True), summary)
        for omitted in ('pch_off_cache_seed', 'repack_run_id'):
            raw = dict(CONTROLS)
            del raw[omitted]
            result, _ = self.run_controls(raw)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_bad_types_and_noncanonical_values_are_not_coerced(self):
        invalid = {'build_jobs': [4.0, True, False, None, 0, 6, '04', '4.0', '+4', ' 4', '4 ', [], {}],
                   'pch_off_cache_seed': [None, False, 0, [], 'cold'],
                   'repack_run_id': [None, False, 0, [], '123']}
        for name in ('preflight_only', 'use_precompiled_headers', 'use_hot_header_pch',
                     'skip_pch_cache_save', 'use_unity_build'):
            invalid[name] = [0, 1, 'true', 'false', None]
        for name, values in invalid.items():
            for value in values:
                with self.subTest(name=name, value=value):
                    result, summary = self.run_controls(dict(CONTROLS, **{name: value}))
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(summary, '')
        for name in ('build_jobs', 'preflight_only', 'use_precompiled_headers',
                     'use_hot_header_pch', 'skip_pch_cache_save', 'use_unity_build'):
            raw = dict(CONTROLS)
            del raw[name]
            result, _ = self.run_controls(raw)
            self.assertNotEqual(result.returncode, 0, name)

    def test_invalid_revision_and_mismatched_checkout_remain_rejected(self):
        for revision in ('', 'A' * 40, 'a' * 39, 'a' * 41, 'g' * 40, 'a' * 40):
            with self.subTest(revision=revision):
                result, summary = self.run_controls(CONTROLS, revision=revision)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(summary, '')

    def test_normal_workflow_defaults_remain_opt_in(self):
        trigger = self.workflow.get('on', self.workflow.get(True))
        inputs = trigger['workflow_dispatch']['inputs']
        self.assertIs(inputs['preflight_only']['default'], True)
        self.assertIs(inputs['use_precompiled_headers']['default'], True)
        self.assertEqual(inputs['build_jobs']['default'], 4)
        for name in ('use_unity_build', 'use_hot_header_pch', 'skip_pch_cache_save'):
            self.assertIs(inputs[name]['default'], False)
        for name in ('pch_off_cache_seed', 'repack_run_id'):
            self.assertEqual(inputs[name]['default'], '')

    def test_unmatched_controls_fail_before_any_dependency_build(self):
        changes = {'preflight_only': True, 'use_precompiled_headers': False,
                   'use_hot_header_pch': True, 'build_jobs': 6,
                   'pch_off_cache_seed': 'cold', 'repack_run_id': '123', 'skip_pch_cache_save': True,
                   'use_unity_build': 'true'}
        for key, value in changes.items():
            with self.subTest(key=key):
                result, summary = self.run_controls(dict(CONTROLS, **{key: value}))
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(summary, '')
        result, _ = self.run_controls(CONTROLS, revision='a' * 40)
        self.assertNotEqual(result.returncode, 0)
        names = [row.get('name') for row in self.job['steps']]
        self.assertLess(names.index('Validate matched performance preview controls'),
                        names.index('Install upstream MSVC v142 toolset'))

    def test_cancelled_compile_records_incomplete_evidence_without_claiming_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            env = dict(os.environ, GITHUB_SHA='a' * 40, COMPILE_OUTCOME='cancelled',
                       JOB_STATUS='cancelled', GITHUB_STEP_SUMMARY=str(root / 'summary'))
            result = subprocess.run([sys.executable, '-B', '-c', self.script('Record performance preview compiler finish state')],
                                    cwd=root, env=env, text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((root / 'build/new-horizons-windows-x64/compile-diagnostics/branch-trial-finish.json').read_text())
            self.assertEqual(report['compileOutcome'], 'cancelled')
            self.assertFalse(report['finalDiagnosticsPresent'])
            self.assertFalse(report['afterCacheStatsPresent'])
            self.assertIn('incomplete trial', report['note'])
        finish = self.steps['Record performance preview compiler finish state']
        self.assertIn('always()', finish['if'])
        self.assertEqual(finish['env']['COMPILE_OUTCOME'], '${{ steps.compile-client.outcome }}')


if __name__ == '__main__':
    unittest.main()
