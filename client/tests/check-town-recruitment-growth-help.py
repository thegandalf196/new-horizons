#!/usr/bin/env python3
"""Source contracts for authoritative growth help, not native/rendered proof."""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RecruitmentGrowthHelpContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.help = (ROOT / 'client/windows/TownGrowthPresentation.cpp').read_text()
        cls.town = (ROOT / 'client/windows/CCastleInterface.cpp').read_text()

    def test_consumes_final_total_unscaled_subtotal_and_actual_percentage(self):
        for field in ('growth.totalGrowth()', 'growth.unscaledGrowth()', 'growth.recruitmentGrowthPercentage'):
            self.assertIn(field, self.help)
        self.assertEqual(self.help.count('growth.totalGrowth()'), 1)
        for forbidden in ('divideAndCeil', 'std::ceil', 'newHorizonsRecruitmentGrowth::scale',
                          'handicapPercentage', '/ 100', '* 100', 'creatures['):
            self.assertNotIn(forbidden, self.help)

    def test_default_historical_and_zero_profiles(self):
        self.assertIn('if(growth.recruitmentGrowthPercentage != 100)', self.help)
        self.assertNotIn('growth.recruitmentGrowthPercentage > 0', self.help)
        self.assertNotIn('== 50', self.help)
        self.assertIn('text.appendTextID("core.genrltxt.589")', self.help)
        self.assertIn('text.replaceNumber(total)', self.help)

    def test_original_contribution_details_and_translation_are_retained(self):
        self.assertIn('for(const auto & entry : growth.entries)', self.help)
        self.assertIn('text.appendRawString(entry.description)', self.help)
        self.assertIn('modifier.toString(translator)', self.help)
        self.assertIn('return text.toString(translator)', self.help)

    def test_town_and_both_fort_layouts_share_breakdown_owner(self):
        self.assertEqual(self.town.count('townGrowthPresentation::description('), 3)
        self.assertIn('return townGrowthPresentation::description(town->getGrowthInfo(level), creature', self.town)
        self.assertIn('town->creatureGrowth(level), true)', self.town)
        self.assertIn('town->creatures[level].first', self.town)

    def test_english_canonical_and_shipped_mirror_agree(self):
        canonical = json.loads((ROOT / 'config/newHorizonsEconomyTexts.json').read_text())
        shipped = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())['translations']
        key = 'new-horizons.growth.recruitmentModifier'
        self.assertEqual(canonical[key], shipped[key])
        self.assertEqual(canonical[key].count('%d'), 3)
        self.assertIn('rounded up', canonical[key])
        self.assertIn('Before recruitment modifier', canonical[key])
        self.assertIn('Final weekly growth', canonical[key])
        self.assertNotIn('50%', canonical[key])

    def test_number_replacement_keeps_one_literal_percent(self):
        # MetaString replaces the first %d literally; it is not printf.
        source = (ROOT / 'lib/texts/MetaString.cpp').read_text()
        self.assertIn('boost::replace_first(dst, "%d", std::to_string(numbers.at(nums++)))', source)
        template = json.loads((ROOT / 'config/newHorizonsEconomyTexts.json').read_text())[
            'new-horizons.growth.recruitmentModifier']
        for percentage, total in ((0, 0), (50, 2), (100, 3), (150, 5)):
            rendered = template
            for value in (3, percentage, total):
                rendered = rendered.replace('%d', str(value), 1)
            self.assertEqual(rendered, 'Before recruitment modifier: 3\n'
                             + f'Recruitment growth: {percentage}% (rounded up)\n'
                             + f'Final weekly growth: {total}')
            self.assertNotIn('%%', rendered)
            self.assertNotIn('%d', rendered)

    def test_build_registration(self):
        cmake = (ROOT / 'client/CMakeLists.txt').read_text()
        for suffix in ('cpp', 'h'):
            self.assertEqual(cmake.count('windows/TownGrowthPresentation.' + suffix), 1)


if __name__ == '__main__':
    unittest.main()
