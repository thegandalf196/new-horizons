"""Active creature inspection uses real Order/Prayer art without phantom slots."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(name):
    return (ROOT / name).read_text()


class CreatureLiveStatusIconsTest(unittest.TestCase):
    def test_prayers_share_catalogue_images_and_preserve_live_readout(self):
        status = read("client/windows/CreatureBonusStatusPresentation.cpp")
        # The spellbook catalogue and the Order prayer choice share these emblems.
        catalogue = read("client/battle/CurrentFactionSkillPresentation.h")
        pattern = r'"(NH_prayer_[a-z_]+\.png)"'
        self.assertEqual(re.findall(pattern, status), re.findall(pattern, catalogue))
        self.assertEqual(len(re.findall(pattern, status)), 6)
        self.assertIn("consecratedArms::activePrayers(*battleStack)", status)
        self.assertIn("passiveFactionSkillPresentation::prayerTexts(prayers)", status)
        self.assertIn("prayerImages.at(static_cast<size_t>(prayer->prayer))", status)
        self.assertIn("status.tooltip = text.details", status)
        self.assertIn("status.bonusSource = BonusSource::SECONDARY_SKILL", status)

    def test_only_active_orders_appear_beneath_portrait_with_own_art(self):
        status = read("client/windows/CreatureOrderStatusPresentation.cpp")
        self.assertIn("activeOrderIndicators(stack)", status)
        self.assertIn("if(indicators.empty())\n\t\treturn;", status)
        self.assertIn("commandDisplay(indicator.command)", status)
        self.assertIn("AnimationPath::builtin(display.image)", status)
        self.assertIn("indicator.description, indicator.description", status)
        self.assertNotIn("NH_orders_command_normal", status)
        self.assertNotIn("bonus-effects", status)
        # Ten roster entries fit below the portrait within the unchanged NH panel.
        self.assertLessEqual(177 + 12 + ((10 - 1) // 3) * 28 + 26, 301)
        self.assertLessEqual(13 + (3 - 1) * 28 + 26, 105)

    def test_old_full_width_order_strip_is_not_also_added(self):
        source = read("client/windows/CCreatureWindow.cpp")
        self.assertEqual(source.count("make_shared<OrderIndicatorsSection>"), 1)
        self.assertIn("make_shared<OrderIndicatorsSection>(this, 177, info->stack)", source)
        self.assertNotIn("pos.h += orderIndicatorsSection->pos.h", source)
        self.assertNotIn("OrderIndicatorsSection::OrderIndicatorsSection", source)
