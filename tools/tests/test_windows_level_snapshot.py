"""Compile the actual level-snapshot declaration; never execute a PE or game."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LevelSnapshotTest(unittest.TestCase):
    def test_actual_snapshot_preserves_hero_level_type(self):
        snapshot = (ROOT / 'client/windows/GUIClasses.h').read_text()
        hero = (ROOT / 'lib/mapObjects/CGHeroInstance.h').read_text()
        declaration = re.search(r'struct PrimaryGainSnapshot\s*\{.*?\};', snapshot, re.S).group()
        snapshot_type = re.search(r'(\w+)\s+level\s*;', declaration).group(1)
        hero_type = re.search(r'(\w+)\s+level\s*;', hero).group(1)
        self.assertEqual(snapshot_type, hero_type)

    def test_actual_aggregate_compiles_without_narrowing(self):
        compiler = shutil.which('cl') or shutil.which('g++')
        if compiler is None:
            self.skipTest('C++ compiler required for compile-only boundary control')
        snapshot = (ROOT / 'client/windows/GUIClasses.h').read_text()
        declaration = re.search(r'struct PrimaryGainSnapshot\s*\{.*?\};', snapshot, re.S).group()
        hero = (ROOT / 'lib/mapObjects/CGHeroInstance.h').read_text()
        hero_type = re.search(r'(\w+)\s+level\s*;', hero).group(1)
        text = ('#include <array>\n#include <cstdint>\n#include <type_traits>\n'
                'using ui32 = std::uint32_t;\n'
                'namespace GameConstants { constexpr int PRIMARY_SKILLS = 4; }\n'
                + declaration + '\n'
                + f'static_assert(std::is_same_v<decltype(PrimaryGainSnapshot::level), {hero_type}>);\n'
                + f'PrimaryGainSnapshot capture(const {hero_type} level) {{ return {{level, {{}}}}; }}\n')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'snapshot.cpp'
            source.write_text(text)
            if Path(compiler).stem.lower() == 'cl':
                command = [compiler, '/nologo', '/std:c++20', '/c', str(source), '/Fo' + str(root / 'snapshot.obj')]
            else:
                command = [compiler, '-std=c++20', '-Werror=narrowing', '-c', str(source), '-o', str(root / 'snapshot.o')]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
