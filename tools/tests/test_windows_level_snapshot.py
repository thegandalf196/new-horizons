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
        self.assertLessEqual(300 + 96 + 84, 516 - 12)
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
        self.assertIn('parentSkill->setHorizontalLayout(228, 34, true)', component)
        self.assertIn('parentSkill->moveTo(pos.topLeft() + Point(8, 48))', component)

    def test_compact_one_and_two_choice_extents_match_recesses(self):
        source = (ROOT / 'client/windows/GUIClasses.cpp').read_text()
        choices = source.split('void CLevelWindow::createSkillBox()', 1)[1]
        perk = source.split('class CLevelUpPerkComponent', 1)[1].split('CLevelWindow::CLevelWindow', 1)[0]
        background = (ROOT / 'client/render/AssetGenerator.cpp').read_text().split(
            'AssetGenerator::CanvasPtr AssetGenerator::createNewHorizonsLevelUpBackground()', 1
        )[1].split('AssetGenerator::CanvasPtr', 1)[0]
        self.assertIn('comp->setHorizontalLayout(244, 84, true)', choices)
        self.assertIn('setHorizontalLayout(244, 46, true)', perk)
        self.assertIn('pos.h = 84', perk)
        for left, category in ((44, 'skillComps'), (352, 'perkComps')):
            self.assertIn(f'Rect({left}, 300, 244, static_cast<int>({category}.size()) * 96 - 12)', choices)
            self.assertIn(f'recessed(Rect({left}, 300 + row * 96, 244, 84))', background)
            for rows in (1, 2):
                box_height = rows * 96 - 12
                for row in range(rows):
                    self.assertLessEqual(row * 96 + 84, box_height)
                self.assertLessEqual(300 + box_height, 516 - 12)
        # Cards must fit before CComponentBox computes its rows; an oversized
        # first card can create an empty row in the unchanged global algorithm.
        self.assertNotIn('setHorizontalLayout(284', choices)
        self.assertNotIn('setHorizontalLayout(284', perk)
        self.assertLess(choices.index('comp->setHorizontalLayout(244, 84, true)'),
                        choices.index('std::make_shared<CComponentBox>(skillComps'))
        self.assertLessEqual(8 + 228, 244)
        self.assertLessEqual(48 + 34, 84)

    def test_centering_is_opt_in_and_moves_the_complete_horizontal_group(self):
        header = (ROOT / 'client/widgets/CComponent.h').read_text()
        source = (ROOT / 'client/widgets/CComponent.cpp').read_text()
        layout = source.split('void CComponent::setHorizontalLayout', 1)[1].split(
            'std::vector<AnimationPath> CComponent::getFileName()', 1)[0]
        self.assertIn('bool centerContent = false', header)
        self.assertIn('if(centerContent)', layout)
        self.assertIn('std::clamp(label->textSize.x, 0, caption.w)', layout)
        self.assertIn('const int groupWidth = image->pos.w + 8 + captionWidth', layout)
        self.assertIn('const int groupLeft = (width - groupWidth) / 2', layout)
        self.assertIn('Point(groupLeft, (height - image->pos.h) / 2)', layout)
        self.assertIn('Point(groupLeft + image->pos.w + 8, 0)', layout)
        self.assertIn('setVisibleSize(Rect(0, 0, captionWidth, height), false)', layout)
        self.assertIn('FONT_TINY, ETextAlignment::CENTERLEFT', layout)
        # Bound arithmetic witnesses for short and wrapped/extended captions.
        # These are source/geometry checks, not rendered glyph measurements.
        for width in (244, 228):
            for icon_width in (32, 42, 44):
                for measured_caption in (0, 25, 100, 500):
                    caption_width = min(max(measured_caption, 0), width - icon_width - 12)
                    group_width = icon_width + 8 + caption_width
                    left = (width - group_width) // 2
                    right = width - left - group_width
                    self.assertGreaterEqual(left, 0)
                    self.assertGreaterEqual(right, 0)
                    self.assertLessEqual(abs(left - right), 1)
                    self.assertLessEqual(left + group_width, width)

    def test_university_confirmation_rank_matches_basic_purchase(self):
        source = (ROOT / 'client/windows/GUIClasses.cpp').read_text()
        confirmation = source.split('CUnivConfirmWindow::CUnivConfirmWindow(', 1)[1].split(
            'void CUnivConfirmWindow::makeDeal(', 1)[0]
        # The existing server grants rank 1 (Basic); the text table is zero-based.
        server = (ROOT / 'server/CGameHandler.cpp').read_text()
        purchase = server.split('bool CGameHandler::buySecSkill(', 1)[1].split(
            'bool CGameHandler::buyHouseOfWisdomScroll(', 1)[0]
        self.assertIn('changeSecSkill(h, skill, 1, ChangeValueMode::ABSOLUTE)', purchase)
        self.assertIn('speechText.replaceTextID("core.skilllev.0")', confirmation)
        self.assertIn('generaltexth->translate("core.skilllev.0")', confirmation)
        self.assertIn('skillName.appendTextID("core.skilllev.0")', confirmation)
        self.assertIn('helpText.replaceTextID("core.skilllev.0")', confirmation)
        self.assertNotIn('core.skilllev.1', confirmation)
        # This is source-contract coverage, not rendered text verification.

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
