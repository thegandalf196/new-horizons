"""Configure-only checks for experimental Unity scope and existing object contracts."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
import build_windows_client as windows


class UnityBuildPolicyTest(unittest.TestCase):
    def configure_policy(self, enabled=None, batch=8, target='BattleAI'):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'probe.cpp').write_text('int unity_probe() { return 1; }\n')
            # LANGUAGES NONE intentionally prevents compiler detection or any
            # compile invocation during this property/argument contract test.
            source = f'''cmake_minimum_required(VERSION 3.16)
project(UnityPolicy NONE)
include("{(ROOT / 'cmake_modules/NewHorizonsUnityBuild.cmake').as_posix()}")
add_library({target} OBJECT probe.cpp)
set_target_properties({target} PROPERTIES LINKER_LANGUAGE CXX)
nh_enable_unity_build({target})
get_target_property(unity {target} UNITY_BUILD)
get_target_property(batch {target} UNITY_BUILD_BATCH_SIZE)
get_source_file_property(guard ForecastRoundIntegration.cpp SKIP_UNITY_BUILD_INCLUSION)
get_source_file_property(attack AttackPossibility.cpp SKIP_UNITY_BUILD_INCLUSION)
get_source_file_property(exchange BattleExchangeVariant.cpp SKIP_UNITY_BUILD_INCLUSION)
file(WRITE "${{CMAKE_BINARY_DIR}}/policy.txt" "${{unity}}\\n${{batch}}\\n${{guard}}\\n${{attack}}\\n${{exchange}}\\n")
'''
            (root / 'CMakeLists.txt').write_text(source)
            command = ['cmake', '-S', str(root), '-B', str(root / 'build'), '-G', 'Ninja',
                       f'-DNH_UNITY_BUILD_BATCH_SIZE={batch}']
            if enabled is not None:
                command.append('-DENABLE_NH_UNITY_BUILD=' + enabled)
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            output = root / 'build/policy.txt'
            cache = root / 'build/CMakeCache.txt'
            if cache.is_file():
                self.assertNotIn('CMAKE_CXX_COMPILER:', cache.read_text())
            return result, output.read_text().splitlines() if output.is_file() else []

    def test_default_remains_normal_and_selected_targets_opt_in(self):
        result, policy = self.configure_policy()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(policy[0], 'unity-NOTFOUND')
        self.assertEqual(policy[2:], ['NOTFOUND'] * 3)
        for target in ('vcmiMain', 'BattleAI', 'vcmiclientcommon'):
            with self.subTest(target=target):
                result, policy = self.configure_policy('ON', target=target)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(policy[:2], ['ON', '8'])
                if target == 'BattleAI':
                    self.assertEqual(policy[2:], ['ON'] * 3)

    def test_batch_boundaries_and_unadmitted_targets_fail_closed(self):
        for batch in range(8, 17):
            with self.subTest(batch=batch):
                result, policy = self.configure_policy('ON', batch)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(policy[:2], ['ON', str(batch)])
        for batch in (0, 7, 17, 'all'):
            with self.subTest(batch=batch):
                result, _ = self.configure_policy('ON', batch)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('must be an integer from 8 through 16', result.stderr)
        result, _ = self.configure_policy('ON', target='unrelatedServer')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not admitted', result.stderr)

    def test_presets_isolate_experiments_and_do_not_change_existing_build_flags(self):
        presets = json.loads((ROOT / 'CMakePresets.json').read_text())
        entries = {row['name']: row for row in presets['configurePresets']}
        for base in ('new-horizons-linux', 'new-horizons-windows-x64'):
            self.assertNotIn('ENABLE_NH_UNITY_BUILD', entries[base]['cacheVariables'])
            unity = entries[base + '-unity']
            self.assertEqual(unity['inherits'], base)
            expected = {'ENABLE_NH_UNITY_BUILD': 'ON', 'NH_UNITY_BUILD_BATCH_SIZE': '8'}
            if 'windows' in base:
                expected['CMAKE_INSTALL_PREFIX'] = unity['binaryDir'] + '/install'
            self.assertEqual(unity['cacheVariables'], expected)
            self.assertNotEqual(unity['binaryDir'], entries[base]['binaryDir'])

    def test_windows_toggle_changes_only_explicit_unity_configuration(self):
        with patch.object(windows.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run:
            self.assertEqual(windows.main(['configure', '--use-precompiled-headers', 'OFF',
                                            '--use-unity-build', 'ON', '--unity-batch-size', '12']), 0)
        command = run.call_args.args[0]
        self.assertIn('-DENABLE_PCH=OFF', command)
        self.assertIn('-DENABLE_NH_UNITY_BUILD=ON', command)
        self.assertIn('-DNH_UNITY_BUILD_BATCH_SIZE=12', command)
        self.assertNotIn('--parallel', command)
        workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        dispatch = workflow.get('on', workflow.get(True))['workflow_dispatch']['inputs']
        self.assertIs(dispatch['use_unity_build']['default'], True)
        steps = {step.get('name'): step for step in workflow['jobs']['build']['steps']}
        expression = "${{ inputs.use_unity_build && 'ON' || 'OFF' }}"
        self.assertIn('--use-unity-build ' + expression, steps['Configure client and internal authority']['run'])
        self.assertIn('!inputs.use_unity_build', steps['Select isolated PCH-off cache']['env']['FULL_PCH_OFF_BUILD'])

    def test_every_early_guard_object_stays_outside_unity_batches(self):
        module = (ROOT / 'cmake_modules/NewHorizonsUnityBuild.cmake').read_text()
        for object_name in windows.GUARD_OBJECT_TARGETS:
            source = object_name.split('BattleAI.dir/', 1)[1].removesuffix('.obj')
            self.assertIn(source, module)
            self.assertTrue((ROOT / 'AI/BattleAI' / source).is_file())
        for name, target in (('lib', 'vcmiMain'), ('AI/BattleAI', 'BattleAI'), ('client', 'vcmiclientcommon')):
            source = (ROOT / name / 'CMakeLists.txt').read_text()
            self.assertEqual(source.count(f'nh_enable_unity_build({target})'), 1)

    def test_standalone_exclusions_name_existing_sources_in_their_own_target(self):
        module = (ROOT / 'cmake_modules/NewHorizonsUnityBuild.cmake').read_text()
        roots = {'vcmiMain': 'lib', 'BattleAI': 'AI/BattleAI', 'vcmiclientcommon': 'client'}
        branches = re.split(r'(?:if|elseif)\(target STREQUAL "([A-Za-z]+)"\)', module)
        for target, body in zip(branches[1::2], branches[2::2]):
            sources = re.findall(r'^\s*([A-Za-z][A-Za-z0-9_/]+\.cpp)\)?$', body, re.MULTILINE)
            self.assertTrue(sources, target)
            for source in sources:
                self.assertTrue((ROOT / roots[target] / source).is_file(), target + '/' + source)


if __name__ == '__main__':
    unittest.main()
