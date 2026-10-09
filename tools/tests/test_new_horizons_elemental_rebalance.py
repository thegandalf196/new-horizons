#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded eight-Elemental data contract; not native combat acceptance."""
import unittest

if __package__:
    from .test_new_horizons_content import load
else:
    from test_new_horizons_content import load
STATS = {
    'airElemental': (12, 10, 7, 10, 45, 10, 14),
    'stormElemental': (13, 11, 8, 11, 50, 10, 15),
    'waterElemental': (11, 12, 7, 10, 50, 6, 9),
    'iceElemental': (12, 14, 9, 12, 60, 6, 10),
    'fireElemental': (14, 9, 9, 12, 40, 8, 12),
    'energyElemental': (15, 10, 11, 14, 45, 10, 13),
    'earthElemental': (10, 14, 8, 11, 55, 4, 6),
    'magmaElemental': (11, 16, 10, 13, 65, 5, 7),
}


def merged(original, patch):
    result = dict(original)
    for key, value in patch.items():
        if value is None:
            result.pop(key, None)
        elif isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = merged(result[key], value)
        else:
            result[key] = value
    return result


class ElementalRebalanceDataTest(unittest.TestCase):
    def setUp(self):
        self.patch = load('Mods/new-horizons/Content/config/creatures/conflux.json')
        self.original = load('config/creatures/conflux.json')
        self.categories = load('config/newHorizonsCreatureCategories.json')
        self.capabilities = load('config/newHorizonsCapabilities.json')
        self.buildings = load('Mods/new-horizons/Content/config/factions/confluxCreatureRanks.json')['core:conflux']['town']['buildings']

    def test_exact_eight_stat_rows_and_economy(self):
        for index, (name, expected) in enumerate(STATS.items()):
            with self.subTest(creature=name):
                row = self.patch['core:' + name]
                actual = (row['attack'], row['defense'], row['damage']['min'], row['damage']['max'],
                          row['hitPoints'], row['speed'], row['initiative'])
                self.assertEqual(actual, expected)
                self.assertEqual(row['cost'], {'gold': 750 if index % 2 else 550})
                self.assertEqual(row['growth'], 4)
                self.assertEqual(self.categories['creatures']['core:' + name], 'elite')

    def test_independent_growth_and_centralized_leadership(self):
        rules = self.capabilities['leadership']
        self.assertEqual((rules['upgradeMultiplierPercent'], rules['upgradeRounding']), (120, 10))
        for base, upgrade in zip(list(STATS)[::2], list(STATS)[1::2]):
            with self.subTest(creature=base):
                self.assertEqual(rules['creatureRequirements']['core:' + base], 250)
                self.assertNotIn('core:' + upgrade, rules['creatureRequirements'])
                self.assertEqual(250 * rules['upgradeMultiplierPercent'] // 100, 300)
                growth = self.categories['growthLines']['core:' + base]
                self.assertEqual(growth['weeklyBaseGrowth'], 4)
                self.assertEqual(growth['members'], ['core:' + base, 'core:' + upgrade])
        self.assertEqual(self.categories['growthLines']['core:fireElemental']['hordeGrowthOverride'], 2)

    def test_all_eight_altars_have_exact_costs_and_only_authored_prerequisites(self):
        for level, rare in enumerate(('gems', 'mercury', 'sulfur', 'crystal'), start=2):
            for upgraded in (False, True):
                key = f'dwelling{"Up" if upgraded else ""}Lvl{level}'
                with self.subTest(building=key):
                    row = self.buildings[key]
                    expected = dict(gold=2500, wood=5, ore=5, mercury=0, sulfur=0, crystal=0, gems=0)
                    expected[rare] = 3 if upgraded else 2
                    self.assertEqual(row['cost#override'], expected)
                    self.assertEqual(row['requires#override'],
                                     ['allOf', [f'dwellingLvl{level}'], ['mageGuild2']]
                                     if upgraded else ['mageGuild1'])
                    if upgraded:
                        self.assertEqual(row['upgrades'], f'dwellingLvl{level}')

    def test_storm_and_ice_attack_roles_and_energy_flight(self):
        storm = merged(self.original['stormElemental'], self.patch['core:stormElemental'])
        ice = merged(self.original['iceElemental'], self.patch['core:iceElemental'])
        energy = merged(self.original['energyElemental'], self.patch['core:energyElemental'])
        storm_types = {ability['type'] for ability in storm['abilities'].values()}
        self.assertEqual(storm['shots'], 16)
        self.assertIn('SHOOTER', storm_types)
        self.assertIn('NO_WALL_PENALTY', storm_types)
        self.assertNotIn('NO_DISTANCE_PENALTY', storm_types)
        self.assertNotIn('NO_MELEE_PENALTY', storm_types)
        self.assertEqual(ice['shots'], 0)
        self.assertNotIn('SHOOTER', {ability['type'] for ability in ice['abilities'].values()})
        self.assertNotIn('missile', ice['graphics'])
        self.assertNotIn('shoot', ice['sound'])
        self.assertIn('FLYING', {ability['type'] for ability in energy['abilities'].values()})
        # No new immunity/school/proc is invented here; Frozen belongs to its runtime owner.
        for name in STATS:
            before = self.original[name]['abilities']
            after = merged(self.original[name], self.patch['core:' + name])['abilities']
            for key, value in before.items():
                if name == 'iceElemental' and key == 'shooter':
                    continue
                self.assertEqual(after[key], value)
            excluded = {'ADDITIONAL_RETALIATION', 'PHYSICAL_DAMAGE_REDUCTION',
                        'PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS', 'RANGED_DAMAGE_REDUCTION'}
            self.assertFalse(excluded & {ability['type'] for ability in after.values()})

    def test_unrelated_conflux_roster_is_preserved(self):
        self.assertEqual(set(self.patch), {'core:' + name for name in STATS} | {'core:pixie', 'wisp', 'wispUpgrade'})
        self.assertEqual(self.patch['core:pixie'], {'upgrades': ['core:sprite']})
        for name, cost, health, growth in (('wisp', 160, 12, 8), ('wispUpgrade', 240, 16, 8)):
            self.assertEqual(self.patch[name]['cost'], {'gold': cost})
            self.assertEqual(self.patch[name]['hitPoints'], health)
            self.assertEqual(self.patch[name]['growth'], growth)
        self.assertNotIn('core:firebird', self.patch)
        self.assertNotIn('core:phoenix', self.patch)


if __name__ == '__main__':
    unittest.main()
