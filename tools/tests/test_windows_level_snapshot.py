"""Compile the actual level-snapshot declaration; never execute a PE or game."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LevelSnapshotTest(unittest.TestCase):
    def test_skill_choice_labels_show_only_offered_rank(self):
        source = (ROOT / 'client/windows/GUIClasses.cpp').read_text()
        choices = source.split('void CLevelWindow::createSkillBox()', 1)[1]
        self.assertIn('translate("core.skilllev", offeredRank - 1)', choices)
        self.assertIn('comp->customSubtitle = offeredRankText + "\\n"', choices)
        self.assertNotIn('new-horizons.levelUp.skill.unlearned', choices)
        self.assertNotIn('new-horizons.levelUp.skill.rankTransition', choices)

    def test_native_panel_footer_has_separate_regions(self):
        source = (ROOT / 'client/windows/GUIClasses.cpp').read_text()
        background = (ROOT / 'client/render/AssetGenerator.cpp').read_text()
        method = background.split(
            'AssetGenerator::CanvasPtr AssetGenerator::createNewHorizonsLevelUpBackground()', 1
        )[1].split('AssetGenerator::CanvasPtr', 1)[0]
        self.assertIn('createDialogBackground(Point(640, 568))', method)
        self.assertIn('PLAYER_COLORED | BORDERED', source)
        self.assertIn('ImagePath::builtin("LVLUPBKG.bmp")', method)
        self.assertIn('original->width() - 36', method)
        self.assertIn('Point((640 - headerWidth) / 2, 18)', method)
        self.assertIn('Rect(18, 18, headerWidth, 166)', method)
        self.assertIn('CHeroArea>(297, 66', source)
        self.assertIn('CLabel>(320, 516', source)
        self.assertIn('CButton>(Point(288, 532)', source)
        self.assertLess(516 + 6, 532)
        self.assertLessEqual(532 + 32, 568)
        self.assertLessEqual(294 + 108 + 96, 516 - 12)
        # CWindowObject::showAll adds these native DIALGBOX border dimensions.
        self.assertLessEqual(640 + 28, 800)
        self.assertLessEqual(568 + 29, 600)

    def test_perk_exposes_distinct_parent_icon_name_and_rank(self):
        source = (ROOT / 'client/windows/GUIClasses.cpp').read_text()
        component = source.split('class CLevelUpPerkComponent', 1)[1].split('CLevelWindow::CLevelWindow', 1)[0]
        self.assertIn('setCustomIcon(icon)', component)
        self.assertIn('CComponent::small', component)
        self.assertIn('parentSkill->customSubtitle = parentName', component)
        self.assertIn('translate("core.skilllev", requiredRank - 1)', component)
        self.assertIn('parentSkill->setHorizontalLayout(226, 40)', component)

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
