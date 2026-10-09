#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Conflux content/asset wiring guards; native tests establish actual growth."""
import json
import unittest

from jsonschema import Draft4Validator, RefResolver
if __package__:
    from .test_new_horizons_content import ROOT, load
    from .nhart_test_resources import ArtPath
else:
    from test_new_horizons_content import ROOT, load
    from nhart_test_resources import ArtPath


class ConfluxGrowthDataTest(unittest.TestCase):
    def setUp(self):
        self.town = load('Mods/new-horizons/Content/config/factions/confluxCreatureRanks.json')['core:conflux']['town']

    def test_restored_core_roster_and_dwellings_are_registered(self):
        self.assertEqual(self.town['creatures'], {
            'modify@1': ['pixie', 'sprite'],
            'modify@6': ['new-horizons:wisp', 'new-horizons:wispUpgrade']})
        overrides = load('Mods/new-horizons/Content/config/creatures/conflux.json')
        self.assertEqual(overrides['core:pixie'], {'upgrades': ['core:sprite']})
        self.assertNotIn('core:psychicElemental', overrides)
        self.assertNotIn('core:magicElemental', overrides)
        module = load('Mods/new-horizons/mod.json')
        self.assertIn('config/factions/confluxCreatureRanks.json', module['factions'])
        self.assertIn('config/creatures/conflux.json', module['creatures'])
        self.assertIsNone(self.town['buildings']['horde1Upgr'])
        self.assertNotIn('dwellingLvl8', self.town['structures'])
        self.assertNotIn('dwellingLvl8', self.town['buildings'])
        self.assertNotIn('dwellingUpLvl1', self.town['structures'])
        self.assertNotIn('dwellingUpLvl1', self.town['buildings'])
        originalTown = load('config/factions/conflux.json')['conflux']['town']
        self.assertEqual(
            originalTown['hallSlots'][3][0], ['dwellingLvl1', 'dwellingUpLvl1'])
        self.assertEqual(originalTown['buildings']['dwellingUpLvl1']['upgrades'], 'dwellingLvl1')
        roster = list(originalTown['creatures'])
        for row, members in self.town['creatures'].items():
            roster[int(row.removeprefix('modify@')) - 1] = members
        categories = load('config/newHorizonsCreatureCategories.json')['creatures']
        bands = [categories[creature if ':' in creature else f'core:{creature}']
                 for members in roster for creature in members]
        self.assertEqual(bands.count('core'), 4, 'Two Core base/upgrade lines')
        self.assertEqual(bands.count('elite'), 8, 'Four Elemental Elite base/upgrade lines')
        self.assertEqual(bands.count('champion'), 2)
        self.assertNotIn('psychicElemental', [creature for members in roster for creature in members])
        self.assertNotIn('magicElemental', [creature for members in roster for creature in members])

    def test_garden_growth_tracks_the_restored_pixie_sprite_core_line(self):
        garden = self.town['buildings']['horde1']
        self.assertEqual(garden['description'],
                         'The Garden of Life increases weekly Pixie and Sprite growth by 4.')
        self.assertNotIn('bonuses', garden, 'The old row-8 Sprite bonus is no longer reachable')
        rules = load('config/newHorizonsCreatureCategories.json')['growthLines']
        self.assertEqual(rules['core:pixie']['hordeGrowthOverride'], 4)
        self.assertEqual(rules['core:pixie']['weeklyBaseGrowth'], 14)
        self.assertEqual(rules['core:pixie']['members'], ['core:pixie', 'core:sprite'])
        self.assertNotIn('core:sprite', rules)
        self.assertEqual(rules['new-horizons:wisp'], {
            'weeklyBaseGrowth': 8,
            'members': ['new-horizons:wisp', 'new-horizons:wispUpgrade']})

    def test_hall_uses_default_pixie_upgrade_and_no_eighth_sprite_card(self):
        slots = self.town['hallSlots']
        self.assertEqual(slots['modify@4'], [
            ['dwellingLvl1', 'dwellingUpLvl1'], ['dwellingLvl6', 'dwellingUpLvl6'],
            ['dwellingLvl2', 'dwellingUpLvl2'], ['dwellingLvl3', 'dwellingUpLvl3']])
        self.assertEqual(slots['modify@5'], [
            ['dwellingLvl4', 'dwellingUpLvl4'], ['dwellingLvl5', 'dwellingUpLvl5'],
            ['dwellingLvl7', 'dwellingUpLvl7']])
        self.assertEqual(slots['modify@3']['modify@3'], ['horde1'])
        self.assertEqual(slots['modify@3']['appendItems'], [['horde2']])
        original = load('config/factions/conflux.json')['conflux']['town']
        self.assertEqual(
            original['hallSlots'][3][0], ['dwellingLvl1', 'dwellingUpLvl1'])
        self.assertNotIn('dwellingLvl8', original['buildings'])

    def test_wisp_dwelling_has_core_access_and_preserves_champion_progression(self):
        buildings = self.town['buildings']
        base = buildings['dwellingLvl6']
        upgrade = buildings['dwellingUpLvl6']
        self.assertEqual(base['name'], 'Altar of Magic')
        self.assertIn('Wisps', base['description'])
        self.assertEqual(base['requires#override'], ['fort'])
        self.assertEqual(base['cost#override'], {
            'gold': 1500, 'wood': 5, 'ore': 5, 'mercury': 0,
            'sulfur': 0, 'crystal': 0, 'gems': 0})
        self.assertEqual(upgrade['upgrades'], 'dwellingLvl6')
        self.assertIn('Greater Wisps', upgrade['description'])
        self.assertEqual(upgrade['requires#override'], ['allOf'])
        self.assertEqual(upgrade['cost#override'], {
            'gold': 1000, 'wood': 0, 'ore': 5, 'mercury': 0,
            'sulfur': 0, 'crystal': 0, 'gems': 0})
        self.assertEqual(buildings['dwellingLvl7']['requires#override'], [
            'allOf', ['dwellingLvl4'], ['dwellingLvl5']])
        # Keep the reused dwelling IDs and original Altar of Magic artwork.
        original = load('config/factions/conflux.json')['conflux']['town']
        self.assertEqual(buildings['dwellingLvl7']['requires#override'],
                         original['buildings']['dwellingLvl6']['requires'])
        self.assertEqual(original['buildings']['dwellingLvl7']['requires'], ['dwellingLvl6'])
        self.assertEqual(original['structures']['dwellingUpLvl6']['animation'], 'TBELUP_5.def')
        self.assertNotIn('dwellingLvl6', self.town['structures'])
        self.assertNotIn('dwellingUpLvl6', self.town['structures'])

    def test_wisp_creatures_use_provisional_stats_and_authored_assets(self):
        creatures = load('Mods/new-horizons/Content/config/creatures/conflux.json')
        categories = load('config/newHorizonsCreatureCategories.json')
        leadership = load('config/newHorizonsCapabilities.json')['leadership']['creatureRequirements']
        expected = {
            'wisp': {
                'name': 'Wisp', 'attack': 7, 'defense': 4, 'minDamage': 2,
                'maxDamage': 3, 'hitPoints': 12, 'speed': 5, 'initiative': 6,
                'growth': 8, 'gold': 160, 'leadership': 140,
                'fightValue': 240, 'aiValue': 260, 'animation': 'Wisp',
                'map': 'WispMap', 'iconLarge': 'Wisp/icons/wisp-icon-58x64.png',
                'iconSmall': 'Wisp/icons/wisp-icon-32.png',
            },
            'wispUpgrade': {
                'name': 'Greater Wisp', 'attack': 9, 'defense': 6, 'minDamage': 3,
                'maxDamage': 4, 'hitPoints': 16, 'speed': 6, 'initiative': 7,
                'growth': 8, 'gold': 240, 'leadership': 170,
                'fightValue': 360, 'aiValue': 400, 'animation': 'WispUpgrade',
                'map': 'WispUpgradeMap', 'iconLarge': 'WispUpgrade/icons/icon-58x64.png',
                'iconSmall': 'WispUpgrade/icons/icon-32x32.png',
            },
        }
        for key, values in expected.items():
            with self.subTest(creature=key):
                creature = creatures[key]
                creature_id = f'new-horizons:{key}'
                self.assertEqual(creature['name']['singular'], values['name'])
                self.assertEqual(creature['faction'], 'core:conflux')
                self.assertEqual(creature['level'], 6)
                self.assertEqual(creature['speed'], values['speed'])
                self.assertEqual(creature['initiative'], values['initiative'])
                self.assertEqual(creature['hitPoints'], values['hitPoints'])
                self.assertEqual(creature['attack'], values['attack'])
                self.assertEqual(creature['defense'], values['defense'])
                self.assertEqual(creature['damage'], {
                    'min': values['minDamage'], 'max': values['maxDamage']})
                self.assertEqual(creature['growth'], values['growth'])
                self.assertEqual(creature['cost'], {'gold': values['gold']})
                self.assertEqual(creature['fightValue'], values['fightValue'])
                self.assertEqual(creature['aiValue'], values['aiValue'])
                self.assertEqual(creature['advMapAmount'], {'min': 8, 'max': 16})
                self.assertFalse(creature['doubleWide'])
                self.assertEqual(categories['creatures'][creature_id], 'core')
                self.assertEqual(leadership[creature_id], values['leadership'])

                abilities = creature['abilities']
                self.assertEqual(abilities['passThrough']['type'], 'PASS_THROUGH')
                self.assertEqual(abilities['longReach'], {'type': 'LONG_REACH', 'val': 5})
                self.assertEqual(abilities['magicProtection'], {
                    'type': 'SPELL_DAMAGE_REDUCTION_BASIS_POINTS', 'subtype': 'any', 'val': 9500})
                self.assertEqual(abilities['noRetaliation']['type'], 'BLOCKS_RETALIATION')
                self.assertNotIn('shooter', abilities)
                self.assertNotIn('canFly', abilities)
                self.assertNotIn('shots', creature)
                self.assertNotIn('missile', creature['graphics'])

                graphics = creature['graphics']
                self.assertEqual(graphics['animation'], values['animation'])
                self.assertEqual(graphics['map'], values['map'])
                self.assertEqual(graphics['mapAttackFromLeft'], f"{values['map']}.def:0:0")
                self.assertEqual(graphics['mapAttackFromRight'], f"{values['map']}.def:0:0")
                self.assertEqual(graphics['iconLarge'], values['iconLarge'])
                self.assertEqual(graphics['iconSmall'], values['iconSmall'])
                sprites = ArtPath()
                images = ArtPath()
                self.assertTrue((sprites / f"{values['animation']}.json").is_file())
                self.assertTrue((sprites / f"{values['map']}.json").is_file())
                self.assertTrue((images / values['iconLarge']).is_file())
                self.assertTrue((images / values['iconSmall']).is_file())
                self.assertEqual(creature['sound'], {
                    'attack': 'MAGICBLT.wav', 'move': 'AELMMOVE.wav', 'wince': 'AELMWNCE.wav'})

        self.assertEqual(creatures['wisp']['upgrades'], ['new-horizons:wispUpgrade'])
        self.assertEqual(creatures['wispUpgrade']['upgrades'], [])

        # Provisional fight/AI values use the same transparent stat proxy for both:
        # twice HP-weighted attack/defense/damage plus a speed/initiative term,
        # rounded to tens, then a modest 10% AI premium. The 2x factor is a
        # provisional allowance for the supplied pass-through/reach/MDR package.
        for key, creature in creatures.items():
            if key not in expected:
                continue
            averageDamage = (creature['damage']['min'] + creature['damage']['max']) / 2
            statProxy = creature['hitPoints'] * (
                averageDamage + 0.25 * (creature['attack'] + creature['defense']))
            statProxy += 5 * (creature['speed'] + creature['initiative'])
            expectedFight = round(2 * statProxy / 10) * 10
            expectedAI = round(1.1 * expectedFight / 10) * 10
            self.assertEqual(creature['fightValue'], expectedFight)
            self.assertEqual(creature['aiValue'], expectedAI)

    def test_wisp_definitions_match_the_creature_schema(self):
        schema = load('config/schemas/creature.json')
        bonusSchema = load('config/schemas/bonusInstance.json')
        resolver = RefResolver.from_schema(schema, store={'bonusInstance.json': bonusSchema})
        validator = Draft4Validator(schema, resolver=resolver)
        creatures = load('Mods/new-horizons/Content/config/creatures/conflux.json')
        for key in ('wisp', 'wispUpgrade'):
            with self.subTest(creature=key):
                validator.validate(creatures[key])

    def test_vault_uses_one_standard_horde_producer_for_the_fire_upgrade_line(self):
        self.assertEqual(self.town['horde']['modify@2'], 3)
        vault = self.town['buildings']['horde2']
        self.assertEqual(vault['name'], 'Vault of Ashes')
        self.assertEqual(vault['requires'], ['dwellingLvl4'])
        self.assertEqual(vault['cost#override']['gold'], 1000)
        self.assertEqual(vault['cost#override']['ore'], 5)
        self.assertNotIn('bonuses', vault, 'Do not double-count Horde growth')
        line = load('config/newHorizonsCreatureCategories.json')['growthLines']['core:fireElemental']
        self.assertEqual(line['weeklyBaseGrowth'], 4)
        self.assertEqual(line['hordeGrowthOverride'], 2)
        self.assertEqual(line['members'], ['core:fireElemental', 'core:energyElemental'])

    def test_original_building_icons_are_referenced_not_replaced_or_extracted(self):
        self.assertEqual(self.town['buildingsIcons'], 'NH_conflux_buildings')
        images = json.loads((ArtPath() / 'NH_conflux_buildings.json').read_text())['images']
        self.assertEqual(len(images), 45)
        frames = {image['frame']: image for image in images}
        self.assertEqual(set(frames), set(range(44)) | {150})
        for frame, image in frames.items():
            self.assertEqual(image, {
                'group': 0, 'frame': frame, 'defFile': 'HALLELEM.DEF',
                'defGroup': 0, 'defFrame': {150: 37, 24: 33}.get(frame, frame)})


if __name__ == '__main__':
    unittest.main()
