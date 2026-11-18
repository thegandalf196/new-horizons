"""Process-free contracts for the shared Windows CI/local compile operation."""
# SPDX-License-Identifier: GPL-2.0-or-later

from contextlib import redirect_stderr
import io
import os
from pathlib import Path
import shlex
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import call, patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
import build_windows_client as build


class WindowsBuildClientTest(unittest.TestCase):
    def test_normal_workflow_builds_guard_objects_before_full_targets(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        step = next(step for job in workflow['jobs'].values() for step in job['steps']
                    if step.get('name') == 'Compile Windows x64 client')
        self.assertEqual(step['if'], "${{ !inputs.preflight_only && inputs.repack_run_id == '' }}")
        invocation = shlex.split(step['run'].replace('\\\n', ''))
        self.assertEqual(invocation[:2], ['python', 'tools/ci/build_windows_client.py'])
        with patch('windows_build_diagnostics.BuildDiagnostics', return_value=None), \
                patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run:
            self.assertEqual(build.main(invocation[2:]), 0)
        guard_targets = [
            'AI/BattleAI/CMakeFiles/BattleAI.dir/' + source + '.obj'
            for source in (
                'ForecastRoundIntegration.cpp', 'ForecastActivationIntegration.cpp',
                'ForecastNativePreparation.cpp', 'ForecastUnitUpdateIntegration.cpp',
                'ForecastEffectIntegration.cpp', 'ForecastSelectedReceiptIntegration.cpp',
                'ForecastAcceptedAttackIntegration.cpp', 'StackPreparedEffectState.cpp',
                'OwnedFrameEffectPublication.cpp',
            )
        ]
        self.assertEqual(run.call_args_list, [
            call([
                'cmake', '--build', 'build/new-horizons-windows-x64', '--target',
                *guard_targets, '--parallel', '4', '--', '-d', 'keeprsp', '-k', '0',
            ], check=False),
            call([
                'cmake', '--build', 'build/new-horizons-windows-x64', '--target',
                'vcmiclient', 'nhWindowsBootstrap', 'nhWindowsSetup', 'nhSdl3GrayscaleMaskRuntimeTest', 'nhArtStartupRuntimeTest',
                'nhImageOptimizerRuntimeTest', 'nhCreaturePreviewSurfaceRuntimeTest',
                'nhSdl3OutlineRuntimeTest', '--parallel', '4', '--', '-d', 'keeprsp',
            ], check=False),
        ])

    def test_portable_entry_smoke_fails_before_expensive_client_compilation(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        steps = workflow['jobs']['build']['steps']
        names = [step.get('name') for step in steps]
        ordered = [names.index(name) for name in (
            'Configure client and internal authority',
            'Verify CMake-selected CRT bytes before compiling',
            'Compile portable entry before the client',
            'Portable entry argument and exit smoke (no game or GUI)',
            'Compile Windows x64 client')]
        self.assertEqual(ordered, sorted(ordered))
        self.assertEqual(names.count('Portable entry argument and exit smoke (no game or GUI)'), 1)
        early = steps[ordered[2]]
        self.assertEqual(early['if'], steps[ordered[4]]['if'])
        self.assertEqual(shlex.split(early['run']), [
            'cmake', '--build', 'build/new-horizons-windows-x64', '--target',
            'nhWindowsBootstrap', 'nhWindowsSetup', '--parallel', '4', '--', '-d', 'keeprsp'])
        smoke = steps[ordered[3]]
        self.assertEqual(smoke['shell'], 'powershell')
        self.assertIn('powershell.exe -NoProfile -File tools/windows/tests/Portable-Entry-Smoke.ps1', smoke['run'])
        self.assertIn('if ($LASTEXITCODE -ne 0)', smoke['run'])
        self.assertIn('tools/windows/tests/Manual-Setup-Smoke.ps1 -SetupPath build/new-horizons-windows-x64/bootstrap/Setup.exe', smoke['run'])

    def test_pch_choice_keeps_default_and_only_controls_normal_configuration(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        dispatch = workflow.get('on', workflow.get(True))['workflow_dispatch']
        setting = dispatch['inputs']['use_precompiled_headers']
        self.assertEqual(setting['type'], 'boolean')
        self.assertIs(setting['default'], True)
        consumers = [step for job in workflow['jobs'].values() for step in job['steps']
                     if 'inputs.use_precompiled_headers' in step.get('run', '')]
        self.assertEqual(len(consumers), 1)
        self.assertEqual(consumers[0]['name'], 'Configure client and internal authority')
        self.assertEqual(consumers[0]['if'], "${{ !inputs.preflight_only && inputs.repack_run_id == '' }}")
        expression = "${{ inputs.use_precompiled_headers && 'ON' || 'OFF' }}"
        self.assertIn('--use-precompiled-headers ' + expression, consumers[0]['run'])
        cases = [('ON', ['configure'])]
        for pch in ('ON', 'OFF'):
            invocation = shlex.split(consumers[0]['run'].replace(expression, pch))
            self.assertEqual(invocation[:2], ['python', 'tools/ci/build_windows_client.py'])
            cases.append((pch, invocation[2:]))
        for pch, argv in cases:
            with self.subTest(pch=pch, argv=argv):
                with patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run:
                    self.assertEqual(build.main(argv), 0)
                run.assert_called_once_with([
                    'cmake', '--preset', 'new-horizons-windows-x64', '-DENABLE_PCH=' + pch,
                    '-DENABLE_CCACHE=ON', '-DENABLE_NH_SDL3_MASK_REGRESSION=ON',
                    '-DENABLE_NH_ART_CACHE_REGRESSION=ON', '-DENABLE_NH_PREVIEW_SURFACE_REGRESSION=ON',
                    '-DENABLE_NH_SDL3_OUTLINE_REGRESSION=ON', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                    '-DCMAKE_C_COMPILER_LAUNCHER=sccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=sccache',
                ], check=False)

    def test_external_cmake_and_paths_with_spaces_preserve_environment_and_exit_status(self):
        environment = dict(os.environ)
        results = [SimpleNamespace(returncode=0), SimpleNamespace(returncode=23)]
        with patch.object(build.subprocess, 'run', side_effect=results) as run:
            status = build.main(['build', '--build-root', 'configured build tree',
                                 '--cmake', 'external tools/cmake', '--jobs', '10'])
        self.assertEqual(status, 23)
        self.assertEqual(dict(os.environ), environment)
        self.assertEqual(run.call_count, 2)
        for index, invocation in enumerate(run.call_args_list):
            command = invocation.args[0]
            self.assertEqual(command[:3], ['external tools/cmake', '--build', 'configured build tree'])
            expected = ['--parallel', '10', '--', '-d', 'keeprsp']
            if index == 0:
                expected.extend(['-k', '0'])
            self.assertEqual(command[command.index('--parallel'):], expected)
            self.assertEqual(invocation.kwargs, {'check': False})

    def test_guard_failure_prevents_full_client_build(self):
        with patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=19)) as run:
            status = build.main(['build', '--build-root', 'configured build tree'])
        self.assertEqual(status, 19)
        run.assert_called_once_with([
            'cmake', '--build', 'configured build tree', '--target',
            *build.GUARD_OBJECT_TARGETS, '--parallel', '4', '--', '-d', 'keeprsp', '-k', '0',
        ], check=False)
        self.assertNotIn('vcmiclient', run.call_args.args[0])

    def test_invalid_job_counts_and_missing_build_root_never_start_a_process(self):
        arguments = [['build', '--build-root', 'configured', '--jobs', value]
                     for value in ('0', '-1', '11', 'all')]
        arguments.extend([[], ['build'], ['configure', '--use-precompiled-headers', 'false']])
        for argv in arguments:
            with self.subTest(argv=argv), redirect_stderr(io.StringIO()):
                with patch.object(build.subprocess, 'run') as run:
                    with self.assertRaises(SystemExit) as error:
                        build.main(argv)
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
