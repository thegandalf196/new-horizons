"""Process-free configuration contracts for the opt-in hot header PCH profile."""
# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools.tests.test_unity_build_policy import ROOT, windows
import yaml


class HotHeaderPchPolicyTest(unittest.TestCase):
    def configure(self, enabled='OFF', pch='ON', target='vcmiMain'):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text('int probe;\n')
            (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(HotHeaderPolicy NONE)
set(ENABLE_PCH {pch})
include("{ROOT / 'cmake_modules/NewHorizonsHotHeaderPch.cmake'}")
add_library({target} OBJECT probe.cpp)
set_target_properties({target} PROPERTIES LINKER_LANGUAGE CXX)
if(ENABLE_PCH)
  target_precompile_headers({target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:<StdInc.h$<ANGLE-R>>")
  nh_enable_hot_header_pch({target})
endif()
get_target_property(headers {target} PRECOMPILE_HEADERS)
file(WRITE "${{CMAKE_BINARY_DIR}}/headers.txt" "${{headers}}")
''')
            result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build'),
                                     '-G', 'Ninja', '-DENABLE_NH_HOT_HEADER_PCH=' + enabled],
                                    capture_output=True, text=True, check=False)
            output = root / 'build/headers.txt'
            cache = root / 'build/CMakeCache.txt'
            if cache.exists():
                self.assertNotIn('CMAKE_CXX_COMPILER:', cache.read_text())
            return result, output.read_text().split(';') if output.exists() else []

    def test_default_and_other_targets_keep_existing_stdinc_only(self):
        for enabled, target in [('OFF', 'vcmiMain'), ('ON', 'unrelatedServer')]:
            result, headers = self.configure(enabled, target=target)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(headers, ['$<$<COMPILE_LANGUAGE:CXX>:<StdInc.h$<ANGLE-R>>'])

    def test_opt_in_appends_hot_headers_after_stdinc_for_only_three_targets(self):
        for target in ('vcmiMain', 'BattleAI', 'vcmiclientcommon'):
            result, headers = self.configure('ON', target=target)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('StdInc.h', headers[0])
            expected = ['lib/mapObjects/CGHeroInstance.h', 'lib/GameLibrary.h',
                        'lib/battle/CBattleInfoCallback.h', 'lib/CStack.h']
            self.assertEqual(headers[1:], [f'$<$<COMPILE_LANGUAGE:CXX>:{ROOT / name}>' for name in expected])
            self.assertEqual(len(headers), len(set(headers)))
            for header in headers[1:]:
                self.assertTrue(header.startswith('$<$<COMPILE_LANGUAGE:CXX>:'))
                self.assertTrue(Path(header.removeprefix('$<$<COMPILE_LANGUAGE:CXX>:').removesuffix('>')).is_file())
        source = (ROOT / 'CMakeLists.txt').read_text()
        self.assertLess(source.index('target_precompile_headers(${name}'), source.index('nh_enable_hot_header_pch(${name})'))

    def test_explicit_hot_headers_cannot_be_silently_disabled_by_pch_off(self):
        result, _ = self.configure('ON', pch='OFF')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('requires ENABLE_PCH=ON', result.stderr)

    def test_workflow_and_cli_independent_toggle_and_diagnostic_identity(self):
        with patch.object(windows.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run:
            self.assertEqual(windows.main(['configure', '--use-hot-header-pch', 'ON']), 0)
        command = run.call_args.args[0]
        self.assertIn('-DENABLE_NH_HOT_HEADER_PCH=ON', command)
        self.assertIn('-DENABLE_PCH=ON', command)
        self.assertIn('-DENABLE_NH_UNITY_BUILD=OFF', command)
        workflow = yaml.safe_load((ROOT / '.github/workflows/new-horizons-windows.yml').read_text())
        setting = workflow.get('on', workflow.get(True))['workflow_dispatch']['inputs']['use_hot_header_pch']
        self.assertIs(setting['default'], False)
        steps = {row.get('name'): row for row in workflow['jobs']['build']['steps']}
        self.assertIn('--use-hot-header-pch', steps['Configure client and internal authority']['run'])
        self.assertIn('!inputs.use_hot_header_pch', steps['Select isolated PCH-off cache']['env']['FULL_PCH_OFF_BUILD'])
        from tools.ci.windows_build_diagnostics import BUILD_SETTINGS
        for key in ('ENABLE_NH_HOT_HEADER_PCH', 'NH_HOT_HEADER_PCH_PROFILE',
                    'NH_HOT_HEADER_PCH_HEADERS_vcmiMain', 'NH_HOT_HEADER_PCH_HEADERS_BattleAI',
                    'NH_HOT_HEADER_PCH_HEADERS_vcmiclientcommon'):
            self.assertIn(key, BUILD_SETTINGS)


if __name__ == '__main__':
    unittest.main()
