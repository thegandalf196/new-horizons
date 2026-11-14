"""Guard level-up source/layout contracts and snapshot ABI; no PE or game execution."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LevelSnapshotTest(unittest.TestCase):
    def test_skill_choice_labels_show_only_offered_rank(self):
        choices = (ROOT / 'client/windows/CLevelWindowChoices.cpp').read_text()
        skill = choices.split('CLevelWindow::skillComponent(size_t index) const', 1)[1].split(
            'CLevelWindow::perkComponent(size_t index) const', 1)[0]
        cards = (ROOT / 'client/windows/LevelUpChoiceCards.cpp').read_text()
        card = cards.split('class CLevelUpSkillCardComponent', 1)[1].split(
            'class CLevelUpPerkComponent', 1)[0]
        self.assertIn('const int offeredRank = hero->getSecSkillLevel(skill) + 1', skill)
        self.assertIn('translate("core.skilllev", offeredRank - 1)', skill)
        self.assertIn('levelUpPresentation::skillCard(skill, offeredRank, skillName, rankText, rows)', skill)
        self.assertIn('component->customSubtitle = rankText + "\\n" + skillName', skill)
        self.assertIn('titleText = offeredRankText + " " + skillName', card)
        self.assertIn('customSubtitle = offeredRankText + "\\n" + skillName', card)
        self.assertIn('newHorizonsHeroes::perkRequiredRank(perk["requires"].String()) != offeredRank', skill)
        self.assertIn('!hero->getPerkState().hasSelection(definition->first, id)', skill)
        for source in (skill, card):
            self.assertNotIn('new-horizons.levelUp.skill.unlearned', source)
            self.assertNotIn('new-horizons.levelUp.skill.rankTransition', source)

    def test_native_panel_footer_has_separate_regions(self):
        source = (ROOT / 'client/windows/CLevelWindow.cpp').read_text()
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
        self.assertIn('CLabel>(320, 286', source)
        self.assertIn('Point(24, 516)', source)
        self.assertIn('Point(66, 516)', source)
        button_x, button_y = map(int, re.search(
            r'ok = std::make_shared<CButton>\(Point\((\d+), (\d+)\)', source).groups())
        description = tuple(map(int, re.search(
            r'perkDescription = std::make_shared<CTextBox>\("", Rect\((\d+), (\d+), (\d+), (\d+)\)',
            source).groups()))
        self.assertEqual((button_x, button_y), (288, 516))
        self.assertEqual(description, (44, 454, 552, 52))
        self.assertEqual(button_x + 32, 640 // 2)
        self.assertLess(296 + 206, button_y)
        self.assertLess(304 + 2 * 80 - 16, description[1])
        self.assertLess(description[1] + description[3], button_y)
        self.assertLessEqual(button_y + 32, 568)
        # Native DIALGBOX borders must still fit the minimum logical viewport.
        self.assertLessEqual(640 + 28, 800)
        self.assertLessEqual(568 + 29, 600)

    def test_perk_exposes_distinct_icon_name_and_parent_rank_header(self):
        cards = (ROOT / 'client/windows/LevelUpChoiceCards.cpp').read_text()
        component = cards.split('class CLevelUpPerkComponent', 1)[1].split(
            'namespace levelUpPresentation', 1)[0]
        choices = (ROOT / 'client/windows/CLevelWindowChoices.cpp').read_text()
        perk = choices.split('CLevelWindow::perkComponent(size_t index) const', 1)[1].split(
            'std::string CLevelWindow::perkHeaderText() const', 1)[0]
        header = choices.split('std::string CLevelWindow::perkHeaderText() const', 1)[1].split(
            'void CLevelWindow::refreshPerkDescription', 1)[0]
        self.assertIn('setCustomIcon(icon)', component)
        self.assertIn('hasSkill ? ComponentType::SEC_SKILL : ComponentType::NONE', component)
        self.assertIn('hasSkill ? skill : SecondarySkill(0), name, CComponent::medium', component)
        self.assertIn('Rect(62, 0, 166, 64)', component)
        self.assertIn('levelUpPresentation::perkCard(', perk)
        self.assertIn('newHorizonsPerkIcon(perk.selection.perkId, perk.name, hero->getPerkState().rules)', perk)
        self.assertIn('newHorizonsPerkHelp::tierName(perk.requiredRank)', perk)
        self.assertIn('newHorizonsPerkHelp::tierName(first.requiredRank) + " "', header)
        self.assertIn('newHorizonsPerkHelp::skillName(hero, first.selection.skillId)', header)
        window = (ROOT / 'client/windows/CLevelWindow.cpp').read_text()
        self.assertIn('Colors::YELLOW, perkHeaderText()', window)
        self.assertIn('refreshPerkDescription(page * 4 + static_cast<size_t>(index))', window)

    def test_one_two_skill_and_one_to_four_perk_extents_fit_regions(self):
        source = (ROOT / 'client/windows/CLevelWindow.cpp').read_text()
        choices = source.split('void CLevelWindow::createSkillBox()', 1)[1].split(
            'void CLevelWindow::setCloseOnSelection', 1)[0]
        cards = (ROOT / 'client/windows/LevelUpChoiceCards.cpp').read_text()
        skill = cards.split('class CLevelUpSkillCardComponent', 1)[1].split(
            'class CLevelUpPerkComponent', 1)[0]
        perk = cards.split('class CLevelUpPerkComponent', 1)[1].split(
            'namespace levelUpPresentation', 1)[0]
        for component, width, height in ((skill, 252, 206), (perk, 236, 64)):
            self.assertIn(f'pos.w = {width}', component)
            self.assertIn(f'pos.h = {height}', component)
        self.assertIn('skillComponent(originalIndex)', choices)
        self.assertIn('perkComponent(originalIndex - skills.size())', choices)
        self.assertLess(choices.index('skillComponent(originalIndex)'),
                        choices.index('std::make_shared<CComponentBox>(skillComps'))
        self.assertIn('skillComps.size() == 1 ? (640 - 252) / 2 : 44', choices)
        self.assertIn('Rect(x, 296, skillComps.size() == 1 ? 252 : 552, 206)', choices)
        self.assertIn('}, 48, 10, 12, 2, false)', choices)
        self.assertIn('perkComps.size() == 1 ? (640 - 236) / 2 : 56', choices)
        self.assertIn('const int rows = static_cast<int>((perkComps.size() + 1) / 2)', choices)
        self.assertIn('Rect(x, 304, perkComps.size() == 1 ? 236 : 528, rows * 80 - 16)', choices)
        self.assertIn('}, 56, 10, 16, 2, true)', choices)
        self.assertIn('page * 4 + 4', choices)
        self.assertIn('std::min<size_t>(perkRows.size(), 4)', skill)
        # Fixed dimensions exist before the box computes rows; even the maximum
        # image gap fits both columns, preventing an empty first row.
        for count in (1, 2):
            width = 252 if count == 1 else 552
            self.assertLessEqual(count * 252 + (count - 1) * 48, width)
            self.assertEqual(2 * (640 - width) // 2 + width, 640)
            self.assertLessEqual(296 + 206, 516 - 12)
        for count in (1, 2, 3, 4):
            rows = (count + 1) // 2
            width = 236 if count == 1 else 528
            columns = min(count, 2)
            self.assertLessEqual(columns * 236 + (columns - 1) * 56, width)
            box_height = rows * 80 - 16
            for row in range(rows):
                self.assertLessEqual(row * 80 + 64, box_height)
            self.assertLess(304 + box_height, 454)
        self.assertLessEqual(60 + 184, 252)
        self.assertLessEqual(58 + 3 * 36 + 5 + 26, 206)
        self.assertLessEqual(62 + 166, 236)

    def test_rerolls_follow_visible_cards_without_covering_choices_or_footer_controls(self):
        # Rerolls remain skill-only, preserve authoritative offer indices, and
        # cannot appear in the mandatory perk modal.
        reroll = (ROOT / 'client/windows/CLevelWindowReroll.cpp').read_text()
        self.assertIn('available && queryID >= 0 && !skills.empty()', reroll)
        self.assertIn('if(!rerollAvailable || !skillChoiceBox)', reroll)
        self.assertIn('for(const auto * child : skillChoiceBox->children)', reroll)
        self.assertIn('dynamic_cast<const CSelectableComponent *>(child)', reroll)
        self.assertIn('skillCardBounds.push_back(child->pos)', reroll)
        self.assertIn('for(const auto originalIndex : displayedChoiceOrder)', reroll)
        self.assertIn('if(originalIndex >= skills.size())', reroll)
        self.assertIn('const auto & card = skillCardBounds[cardIndex++]', reroll)
        self.assertIn('requestReroll(originalIndex)', reroll)
        self.assertIn('rerollButtons.emplace_back(originalIndex, std::move(button))', reroll)
        self.assertIn('AnimationPath::builtin("settingsWindow/button80")', reroll)
        self.assertIn('skillCardBounds.size() == 1 ? card.x + 10 : card.x + (card.w - button->pos.w) / 2', reroll)
        self.assertIn('button->moveTo(Point(buttonX, card.y + card.h + 14))', reroll)
        request = reroll.split('void CLevelWindow::requestReroll(size_t skillIndex)', 1)[1].split(
            'void CLevelWindow::rerollRequestApplied', 1)[0]
        self.assertIn('rerollPending || selectionSubmitted || !rerollCallback', request)
        self.assertLess(request.index('rerollPending = true'), request.index('rerollCallback(skillIndex)'))
        component_box = (ROOT / 'client/widgets/CComponent.cpp').read_text()
        self.assertIn('std::max(betweenSubtitlesMin, betweenImagesMin - subtitlesOffset)', component_box)
        self.assertIn('int freeSpace = (pos.w - rows_row.width) / ((int)rows_row.comps * 2)', component_box)
        # Source/geometry witnesses, not rendered glyph measurements. Native
        # button80 is 80x32; paging is 32x32 and IOKAY is 64x32.
        for count in (1, 2):
            for icon_width in (32, 42, 44):
                width = 252 if count == 1 else 552
                left = (640 - width) // 2
                distance = max(10, 48 - 2 * ((252 - icon_width) // 2))
                row_width = count * 252 + (count - 1) * distance
                free_space = (width - row_width) // (2 * count)
                current_x = 0
                for column in range(count):
                    current_x += free_space + (distance if column else 0)
                    card_x = left + current_x
                    button_x = card_x + (10 if count == 1 else (252 - 80) // 2)
                    button_y = 296 + 206 + 14
                    self.assertGreaterEqual(button_x, card_x)
                    self.assertLessEqual(button_x + 80, card_x + 252)
                    self.assertGreater(button_y, 296 + 206)
                    self.assertLessEqual(button_y + 32, 568)
                    for control_x, control_width in ((24, 32), (66, 32), (288, 64)):
                        self.assertTrue(button_x + 80 <= control_x or control_x + control_width <= button_x)
                    current_x += 252 + free_space

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
        for width in (156, 244, 228):
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
        snapshot = (ROOT / 'client/windows/CLevelWindow.h').read_text()
        hero = (ROOT / 'lib/mapObjects/CGHeroInstance.h').read_text()
        declaration = re.search(r'struct PrimaryGainSnapshot\s*\{.*?\};', snapshot, re.S).group()
        snapshot_type = re.search(r'(\w+)\s+level\s*;', declaration).group(1)
        hero_type = re.search(r'(\w+)\s+level\s*;', hero).group(1)
        self.assertEqual(snapshot_type, hero_type)

    def test_actual_aggregate_compiles_without_narrowing(self):
        compiler = shutil.which('cl') or shutil.which('g++')
        if compiler is None:
            self.skipTest('C++ compiler required for compile-only boundary control')
        snapshot = (ROOT / 'client/windows/CLevelWindow.h').read_text()
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
