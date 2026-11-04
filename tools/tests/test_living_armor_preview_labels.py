#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Two-quantity Living Armor labels; no rendered geometry or spell arithmetic claim."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = 'new-horizons.spellbook.effect.livingArmor'
KEYS = (BASE, BASE + '.short', BASE + '.short.narrow')


def format_contract_label(label, quantities):
    """Require the two raw-string substitutions used by the shared preview consumer."""
    if re.findall(r'%(?:.|$)', label) != ['%s', '%s'] or len(quantities) != 2:
        raise ValueError('Living Armor requires HP and duration string parameters')
    return label % tuple(quantities)


class LivingArmorPreviewLabelsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.texts = json.loads((ROOT / 'config/newHorizonsCombatTexts.json').read_text())

    def test_all_label_sizes_consume_hp_then_rounds(self):
        expected = ('Summon 110 HP • 3 rounds', '110 HP • 3r', '110HP/3r')
        for key, result in zip(KEYS, expected):
            self.assertEqual(format_contract_label(self.texts[key], ('110', '3')), result)
        self.assertLess(len(self.texts[KEYS[2]]), len(self.texts[KEYS[1]]))
        self.assertLess(len(self.texts[KEYS[1]]), len(self.texts[KEYS[0]]))

    def test_missing_extra_and_wrong_placeholder_types_are_rejected(self):
        for invalid in ('Summon HP', 'Summon %s HP', '%s HP / %s rounds / %s',
                        '%d HP / %s rounds', '%s HP / %d rounds',
                        '%1$s HP / %2$s rounds', '%%s HP / %s rounds'):
            with self.subTest(label=invalid), self.assertRaises(ValueError):
                format_contract_label(invalid, ('110', '3'))

    def test_missing_and_extra_arguments_are_rejected(self):
        for quantities in ((), ('110',), ('110', '3', 'extra')):
            with self.subTest(arguments=quantities), self.assertRaises(ValueError):
                format_contract_label(self.texts[BASE], quantities)

    def test_current_shared_producer_and_consumer_expose_both_quantities(self):
        preview = (ROOT / 'lib/spells/SpellEffectPreview.cpp').read_text()
        start = preview.index('if(key == "new-horizons:summonLivingArmor" && result.primary)')
        living = preview[start:preview.index('if(spell.getId() == SpellID::EARTHQUAKE', start)]
        self.assertIn('result.secondary = SpellEffectPreview::Quantity{', living)
        self.assertIn('newHorizonsMetamagic::outgoingDuration(ordinary, &cast, nullptr)', living)
        source = (ROOT / 'client/windows/SpellbookEntryPresentation.cpp').read_text()
        start = source.index('std::string effectText(')
        formatter = source[start:source.index('\n}\n}', start)]
        self.assertLess(formatter.index('if(preview.primary)'), formatter.index('if(preview.secondary)'))
        self.assertEqual(formatter.count('text.replaceRawString(number('), 2)

    def test_generated_module_has_exact_current_labels(self):
        module = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
        for key in KEYS:
            self.assertEqual(module['translations'][key], self.texts[key])


if __name__ == '__main__':
    unittest.main()
