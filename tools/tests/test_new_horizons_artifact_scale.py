#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard the complete New Horizons primary/Movement artifact overlay."""
import re
import unittest

from test_new_horizons_content import load


PRIMARY_NAMES = {
    'attack': 'Attack',
    'defence': 'Defense',
    'spellpower': 'Spell Power',
    'knowledge': 'Knowledge',
}
MOVEMENT_NAMES = {
    'heroMovementLand': 'land Movement',
    'heroMovementSea': 'sea Movement',
}
PRIMARY_SKILLS = set(PRIMARY_NAMES)


def converted_artifact_inventory(artifacts):
    """Map artifact ID -> bonus ID -> (target value, source bonus)."""
    inventory = {}
    for artifact_id, artifact in artifacts.items():
        converted = {}
        for bonus_id, bonus in (artifact.get('bonuses') or {}).items():
            if (bonus.get('type') == 'PRIMARY_SKILL'
                    and 'DRAGON_NATURE' not in bonus.get('limiters', [])):
                target = bonus['val'] * 5
            elif (bonus.get('type') == 'MOVEMENT'
                    and bonus.get('subtype') in MOVEMENT_NAMES):
                value = bonus['val']
                if value % 10:
                    raise AssertionError(
                        f'{artifact_id}.{bonus_id}: Movement value {value} is not divisible by ten')
                target = value // 10
            else:
                continue
            converted[bonus_id] = (target, bonus)
        if converted:
            inventory['core:' + artifact_id] = converted
    return inventory


class NewHorizonsArtifactScaleDataTest(unittest.TestCase):
    def test_overlay_covers_exact_primary_and_flat_movement_inventory(self):
        base = load('config/artifacts.json')
        overlay = load(
            'Mods/new-horizons/Content/config/artifacts/scaledAttributesAndMovement.json')
        inventory = converted_artifact_inventory(base)

        self.assertEqual(sum(len(bonuses) for bonuses in inventory.values()), 78)
        self.assertEqual(len(inventory), 43)
        self.assertEqual(set(overlay), set(inventory))
        self.assertNotIn('core:vialOfDragonBlood', overlay)

        for artifact_id, records in inventory.items():
            with self.subTest(artifact=artifact_id):
                entry = overlay[artifact_id]
                bonus_overrides = entry.get('bonuses', {})
                self.assertEqual(set(bonus_overrides), set(records))
                for bonus_id, (expected, _source) in records.items():
                    self.assertEqual(bonus_overrides[bonus_id], {'val': expected})
                description = entry.get('text', {}).get('description')
                self.assertIsInstance(description, str)
                self.assertTrue(description.strip())
                self._assert_description_matches_records(artifact_id, records, description)

    def test_vial_keeps_its_dragon_limited_primary_bonuses_out_of_the_overlay(self):
        vial = load('config/artifacts.json')['vialOfDragonBlood']['bonuses']
        self.assertEqual(set(vial), {'attack', 'defence'})
        for bonus in vial.values():
            self.assertEqual(bonus['type'], 'PRIMARY_SKILL')
            self.assertIn('DRAGON_NATURE', bonus['limiters'])
            self.assertEqual(bonus['val'], 5)

    def _assert_description_matches_records(self, artifact_id, records, description):
        if artifact_id == 'core:powerOfTheDragonFather':
            for phrase in (
                    'Attack and Defense by 75 each',
                    'Spell Power and Knowledge by 80 each',
                    'Morale and Luck by 1 each',
                    'immunity to spells of levels 1 through 4'):
                self.assertIn(phrase, description)
            return

        if artifact_id == 'core:seaCaptainsHat':
            for phrase in (
                    'sea Movement by 50', 'protects the hero from whirlpools',
                    'grants access to Summon Boat',
                    'Other legacy spell grants do not bypass New Horizons spell-availability and daily-use rules'):
                self.assertIn(phrase, description)

        if artifact_id == 'core:armageddonsBlade':
            self.assertIn('grants expert armageddon', description.lower())
            self.assertIn('immunity to armageddon', description.lower())

        names = {record[1].get('subtype') for record in records.values()}
        values = {record[0] for record in records.values()}
        if names == PRIMARY_SKILLS and len(values) == 1:
            value = next(iter(values))
            self.assertIn(f'four Primary Attributes by {value}', description)
            return

        for _bonus_id, (value, source) in records.items():
            if source['type'] == 'PRIMARY_SKILL':
                name = PRIMARY_NAMES[source['subtype']]
            else:
                name = MOVEMENT_NAMES[source['subtype']]
            verb = 'increases' if value >= 0 else 'decreases'
            phrase = re.compile(
                rf'\b{verb}\b[^.]*\b{re.escape(name)}\b[^.]*\bby {abs(value)}\b',
                re.IGNORECASE)
            self.assertRegex(description, phrase.pattern)


if __name__ == '__main__':
    unittest.main()
