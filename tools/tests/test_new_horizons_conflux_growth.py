#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Conflux content/asset wiring guards; native tests establish actual growth."""
import unittest

from test_new_horizons_content import load


class ConfluxGrowthDataTest(unittest.TestCase):
    def setUp(self):
        self.town = load('Mods/new-horizons/Content/config/factions/confluxCreatureRanks.json')['core:conflux']['town']

    def test_independent_core_rows_are_registered_without_a_pixie_upgrade(self):
        self.assertEqual(self.town['creatures'], {
            'modify@1': ['pixie'], 'appendItems': [['sprite']]})
        self.assertEqual(load('Mods/new-horizons/Content/config/creatures/conflux.json'), {
            'core:pixie': {'upgrades': []}})
        module = load('Mods/new-horizons/mod.json')
        self.assertIn('config/factions/confluxCreatureRanks.json', module['factions'])
        self.assertIn('config/creatures/conflux.json', module['creatures'])
        self.assertIsNone(self.town['buildings']['dwellingUpLvl1'])
        self.assertIsNone(self.town['buildings']['horde1Upgr'])

    def test_only_the_garden_grants_the_additional_sprite_growth(self):
        grove = self.town['buildings']['dwellingLvl8']
        self.assertEqual(grove['requires'], ['fort'])
        self.assertEqual(grove['cost#override']['gold'], 1000)
        self.assertEqual(grove['cost#override']['wood'], 5)
        self.assertNotIn('bonuses', grove)
        self.assertEqual(self.town['buildings']['horde1']['bonuses'], [{
            'type': 'CREATURE_GROWTH', 'subtype': 'creatureLevel8', 'val': 3}])
        rules = load('config/newHorizonsCreatureCategories.json')['growthLines']
        self.assertEqual(rules['core:pixie']['hordeGrowthOverride'], 4)
        self.assertEqual(rules['core:pixie']['weeklyBaseGrowth'], 14)
        self.assertEqual(rules['core:sprite']['weeklyBaseGrowth'], 10)

    def test_hall_exposes_both_dwellings_without_obsolete_upgrade_cards(self):
        slots = self.town['hallSlots']
        self.assertEqual(slots['modify@4']['modify@1'], ['dwellingLvl1'])
        self.assertEqual(slots['modify@5']['appendItems'], [['dwellingLvl8']])
        self.assertEqual(slots['modify@3']['modify@3'], ['horde1'])

    def test_original_building_icons_are_referenced_not_replaced_or_extracted(self):
        self.assertEqual(self.town['buildingsIcons'], 'NH_conflux_buildings')
        images = load('Mods/new-horizons/Images/NH_conflux_buildings.json')['images']
        self.assertEqual(len(images), 45)
        frames = {image['frame']: image for image in images}
        self.assertEqual(set(frames), set(range(44)) | {150})
        for frame, image in frames.items():
            self.assertEqual(image, {
                'group': 0, 'frame': frame, 'defFile': 'HALLELEM.DEF',
                'defGroup': 0, 'defFrame': 37 if frame == 150 else frame})


if __name__ == '__main__':
    unittest.main()
