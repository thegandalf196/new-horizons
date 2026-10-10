#!/usr/bin/env python3
"""Hero-data shape checks; native resolution and GUI acceptance are separate gates."""
import json
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[2]

CANONICAL_STARTS = {
    'core:knight': [30, 45, 10, 15],
    'core:cleric': [10, 15, 30, 45],
    'core:ranger': [35, 35, 15, 15],
    'core:druid': [5, 10, 30, 55],
    'core:alchemist': [30, 20, 20, 30],
    'core:wizard': [5, 5, 45, 45],
    'core:demoniac': [55, 20, 20, 5],
    'core:heretic': [20, 5, 50, 25],
    'core:deathknight': [45, 20, 30, 5],
    'core:necromancer': [5, 20, 50, 25],
    'core:overlord': [50, 25, 20, 5],
    'core:warlock': [15, 5, 60, 20],
    'core:barbarian': [55, 35, 5, 5],
    'core:battlemage': [45, 5, 30, 20],
    'core:beastmaster': [35, 55, 5, 5],
    'core:witch': [5, 15, 20, 60],
    'core:planeswalker': [35, 20, 30, 15],
    'core:elementalist': [5, 5, 60, 30],
}

CANONICAL_SKILLS = (
    'new-horizons:offense', 'new-horizons:armorer', 'new-horizons:archery',
    'new-horizons:battlecraft', 'new-horizons:warMachines', 'new-horizons:discipline',
    'new-horizons:recruitment', 'new-horizons:command', 'new-horizons:warcasting',
    'new-horizons:spellcraft', 'new-horizons:wisdom', 'new-horizons:lightMagic',
    'new-horizons:shadowMagic', 'new-horizons:natureMagic', 'new-horizons:havocMagic',
    'new-horizons:sorceryMagic', 'new-horizons:chaosMagic', 'new-horizons:logistics',
    'new-horizons:diplomacy', 'new-horizons:estates', 'new-horizons:learning',
    'new-horizons:luck', 'new-horizons:divineMandate', 'new-horizons:sylvanLuck',
    'new-horizons:metamagic', 'new-horizons:demonicGating', 'new-horizons:necromancy',
    'new-horizons:shroudOfMalassa', 'new-horizons:bloodrage',
    'new-horizons:bulwarkOfTheMire', 'new-horizons:elementalRebirth',
)

CANONICAL_WEIGHT_ROWS = {
    'core:knight': '4 6 3 5 3 5 4 5 4 2 0 5 1 1 1 4 1 6 8 7 3 5 10 0 0 0 0 0 0 0 0',
    'core:cleric': '1 3 1 2 2 4 2 0 4 6 7 8 2 2 2 6 2 4 8 6 7 5 10 0 0 0 0 0 0 0 0',
    'core:ranger': '4 4 6 5 2 3 2 4 7 3 0 5 1 7 2 1 1 9 5 3 4 8 0 10 0 0 0 0 0 0 0',
    'core:druid': '1 1 2 1 1 2 2 0 1 7 8 7 2 10 2 2 2 5 5 3 8 10 0 10 0 0 0 0 0 0 0',
    'core:alchemist': '3 2 3 4 5 2 1 5 10 7 0 1 1 1 7 7 1 5 4 6 7 3 0 0 10 0 0 0 0 0 0',
    'core:wizard': '1 1 1 1 3 2 1 0 1 9 9 1 1 1 8 10 1 3 4 7 10 2 0 0 10 0 0 0 0 0 0',
    'core:demoniac': '8 3 2 6 2 3 5 6 4 2 0 1 1 1 5 1 4 7 1 5 2 4 0 0 0 10 0 0 0 0 0',
    'core:heretic': '3 1 2 3 2 2 2 0 4 6 7 1 2 1 8 2 8 4 2 4 7 4 0 0 0 10 0 0 0 0 0',
    'core:deathknight': '6 4 2 5 2 4 2 5 7 4 0 1 6 1 2 3 3 6 2 4 4 2 0 0 0 0 10 0 0 0 0',
    'core:necromancer': '1 3 1 2 3 2 3 0 4 6 7 1 9 1 2 6 3 4 1 6 9 1 0 0 0 0 10 0 0 0 0',
    'core:overlord': '6 5 3 6 3 3 3 6 4 2 0 1 4 1 5 1 1 8 2 5 3 5 0 0 0 0 0 10 0 0 0',
    'core:warlock': '3 1 2 3 2 1 3 0 4 7 7 1 7 1 10 1 1 5 1 5 8 4 0 0 0 0 0 10 0 0 0',
    'core:barbarian': '8 4 5 8 2 3 5 5 1 1 0 1 1 2 1 1 3 9 1 2 2 6 0 0 0 0 0 0 10 0 0',
    'core:battlemage': '6 1 4 6 1 3 4 0 10 4 5 1 1 6 1 1 6 7 3 2 6 8 0 0 0 0 0 0 10 0 0',
    'core:beastmaster': '4 8 5 7 2 6 4 4 1 1 0 1 2 3 1 1 1 6 2 3 2 5 0 0 0 0 0 0 0 10 0',
    'core:witch': '1 4 2 2 1 3 2 0 4 6 8 1 8 8 1 1 2 5 3 4 8 7 0 0 0 0 0 0 0 10 0',
    'core:planeswalker': '4 2 4 6 2 2 1 4 10 7 0 1 1 7 7 1 1 10 4 3 6 9 0 0 0 0 0 0 0 0 10',
    'core:elementalist': '1 1 1 1 2 2 2 0 1 9 9 1 1 8 10 1 1 5 4 4 9 8 0 0 0 0 0 0 0 0 10',
}

CANONICAL_CLASS_NAMES = {
    'core:alchemist': 'Battle Mage',
    'core:battlemage': 'Shaman',
    'core:demoniac': 'Tyrant',
    'core:heretic': 'Cultist',
}


# Authored workbook packages added after the first 48 settled profiles.
# Troops, books, biographies and specialty riders are outside this table.
WORKBOOK_REMAINING_STARTS = {
    'core:orrin': 'archery.targetCaller',
    'core:valeska': 'archery.pointBlankShot',
    'core:edric': 'armorer.countercharge',
    'core:lordHaart': 'estates.taxCollector',
    'core:sorsha': 'offense.executioner',
    'core:christian': 'warMachines.masterGunner',
    'core:tyris': 'battlecraft.tactics',
    'core:adela': 'lightMagic.benediction',
    'core:cuthbert': 'spellcraft.spellPenetration',
    'core:loynis': 'lightMagic.benediction',
    'core:rion': 'warMachines.surgeon',
    'core:mephala': 'armorer.pavise',
    'core:ufretin': 'warcasting.spellward',
    'core:jenova': 'estates.landSurveyor',
    'core:ryland': 'diplomacy.negotiator',
    'core:ivor': 'archery.targetCaller',
    'core:kyrre': 'logistics.scouting',
    'core:uland': 'lightMagic.healer',
    'core:gem': 'warMachines.surgeon',
    'core:melodia': 'luck.fortuneSFavor',
    'core:alagar': 'havocMagic.cryomancer',
    'core:josephine': 'armorer.pavise',
    'core:neela': 'armorer.ironDiscipline',
    'core:fafner': 'warcasting.spellward',
    'core:halon': 'warcasting.battleMeditation',
    'core:rissa': 'estates.prospector',
    'core:theodorus': 'learning.mentor',
    'core:solmyr': 'havocMagic.stormcaller',
    'core:cyra': 'sorceryMagic.temporalist',
    'core:rashka': 'offense.shockAssault',
    'core:marius': 'armorer.ironDiscipline',
    'core:octavia': 'estates.taxCollector',
    'core:calh': 'archery.targetCaller',
    'core:pyre': 'warMachines.masterGunner',
    'core:nymus': 'offense.executioner',
    'core:olema': 'spellcraft.concentration',
    'core:calid': 'estates.prospector',
    'core:xarfax': 'havocMagic.pyromancer',
    'core:zydar': 'spellcraft.spellPenetration',
    'core:vokial': 'offense.executioner',
    'core:moandor': 'archery.targetCaller',
    'core:tamika': 'offense.shockAssault',
    'core:clavius': 'estates.taxCollector',
    'core:galthran': 'offense.executioner',
    'core:aislinn': 'spellcraft.concentration',
    'core:sandro': 'spellcraft.spellPenetration',
    'core:xsi': 'shadowMagic.witheringTouch',
    'core:lorelei': 'logistics.scouting',
    'core:arlach': 'warMachines.masterGunner',
    'core:dace': 'offense.shockAssault',
    'core:ajit': 'warcasting.spellward',
    'core:damacon': 'estates.taxCollector',
    'core:gunnar': 'logistics.scouting',
    'core:synca': 'recruitment.drillSergeant',
    'core:shakti': 'offense.executioner',
    'core:malekith': 'spellcraft.spellPenetration',
    'core:darkstorn': 'wisdom.intelligence',
    'core:yog': 'warMachines.precisionBombardment',
    'core:gurnisson': 'warMachines.masterGunner',
    'core:jabarkas': 'archery.targetCaller',
    'core:shiva': 'logistics.scouting',
    'core:gretchin': 'logistics.pathfinding',
    'core:krellion': 'discipline.steadfast',
    'core:cragHack': 'offense.shockAssault',
    'core:tyraxor': 'battlecraft.tactics',
    'core:gird': 'spellcraft.arcaneFocus',
    'core:vey': 'discipline.inspirationalLeader',
    'core:dessa': 'logistics.pathfinding',
    'core:zubin': 'archery.targetCaller',
    'core:gundula': 'offense.executioner',
    'core:bron': 'armorer.countercharge',
    'core:drakon': 'discipline.steadfast',
    'core:wystan': 'archery.targetCaller',
    'core:tazar': 'armorer.pavise',
    'core:alkin': 'offense.executioner',
    'core:korbac': 'logistics.pathfinding',
    'core:gerwulf': 'warMachines.masterGunner',
    'core:broghild': 'logistics.scouting',
    'core:verdish': 'warMachines.surgeon',
    'core:merist': 'natureMagic.herbalist',
    'core:styg': 'spellcraft.spellPenetration',
    'core:pasis': 'offense.executioner',
    'core:thunar': 'armorer.pavise',
    'core:ignissa': 'offense.shockAssault',
    'core:monere': 'logistics.scouting',
    'core:erdamon': 'armorer.countercharge',
    'core:fiur': 'offense.executioner',
    'core:kalt': 'discipline.steadfast',
    'core:luna': 'havocMagic.pyromancer',
    'core:brissa': 'natureMagic.rootcaller',
    'core:ciele': 'havocMagic.stormcaller',
    'core:labetha': 'natureMagic.rootcaller',
    'core:inteus': 'havocMagic.pyromancer',
    'core:aenain': 'natureMagic.rootcaller',
    'core:gelare': 'estates.merchantPrince',
    'core:grindan': 'estates.prospector',
}

# These settled profiles intentionally take precedence over workbook differences.
ACCEPTED_STARTS = {
    'core:sylvia': ('logistics.navigation', 1, 'divineMandate', 1),
    'core:adelaide': ('havocMagic.cryomancer', 1, 'divineMandate', 2),
    'core:ingham': ('wisdom.mysticism', 1, 'divineMandate', 1),
    'core:sanya': ('learning.eagleEye', 1, 'divineMandate', 1),
    'core:caitlin': ('wisdom.intelligence', 1, 'divineMandate', 1),
    'core:thorgrim': ('warcasting.spellward', 2, 'sylvanLuck', 1),
    'core:clancy': ('logistics.pathfinding', 1, 'sylvanLuck', 1),
    'core:coronius': ('spellcraft.concentration', 1, 'sylvanLuck', 1),
    'core:elleshar': ('wisdom.intelligence', 1, 'sylvanLuck', 1),
    'core:malcom': ('learning.eagleEye', 1, 'sylvanLuck', 1),
    'core:aeris': ('logistics.scouting', 1, 'sylvanLuck', 1),
    'core:piquedram': ('logistics.scouting', 1, 'metamagic', 1),
    'core:torosar': ('warMachines.masterGunner', 1, 'metamagic', 1),
    'core:iona': ('learning.scholar', 1, 'metamagic', 1),
    'core:astral': ('spellcraft.concentration', 1, 'metamagic', 2),
    'core:serena': ('learning.eagleEye', 1, 'metamagic', 1),
    'core:daremyth': ('luck.secondChance', 1, 'metamagic', 1),
    'core:aine': ('estates.taxCollector', 1, 'metamagic', 1),
    'core:thane': ('learning.scholar', 2, 'metamagic', 1),
    'core:fiona': ('logistics.scouting', 2, 'demonicGating', 1),
    'core:ignatius': ('battlecraft.tactics', 1, 'demonicGating', 1),
    'core:ayden': ('wisdom.intelligence', 1, 'demonicGating', 1),
    'core:xyron': ('havocMagic.pyromancer', 1, 'demonicGating', 1),
    'core:axsis': ('wisdom.mysticism', 1, 'demonicGating', 1),
    'core:ash': ('chaosMagic.frenziedCurse', 1, 'demonicGating', 1),
    'core:straker': ('warcasting.spellward', 1, 'necromancy', 1),
    'core:charna': ('battlecraft.tactics', 1, 'necromancy', 1),
    'core:isra': ('learning.historian', 1, 'necromancy', 2),
    'core:septienna': ('spellcraft.arcaneFocus', 1, 'necromancy', 1),
    'core:nimbus': ('learning.eagleEye', 1, 'necromancy', 1),
    'core:thant': ('wisdom.mysticism', 1, 'necromancy', 1),
    'core:vidomina': ('learning.scholar', 1, 'necromancy', 2),
    'core:nagash': ('estates.taxCollector', 1, 'necromancy', 1),
    'core:alamar': ('shadowMagic.bloodDrinker', 1, 'shroudOfMalassa', 1),
    'core:jaegar': ('wisdom.mysticism', 1, 'shroudOfMalassa', 1),
    'core:jeddite': ('spellcraft.concentration', 1, 'shroudOfMalassa', 2),
    'core:geon': ('learning.eagleEye', 1, 'shroudOfMalassa', 1),
    'core:deemer': ('logistics.scouting', 2, 'shroudOfMalassa', 1),
    'core:sephinroth': ('estates.prospector', 1, 'shroudOfMalassa', 1),
    'core:terek': ('battlecraft.tactics', 1, 'bloodrage', 1),
    'core:oris': ('learning.eagleEye', 1, 'bloodrage', 1),
    'core:saurug': ('estates.prospector', 1, 'bloodrage', 1),
    'core:mirlanda': ('shadowMagic.witheringTouch', 1, 'bulwarkOfTheMire', 2),
    'core:rosic': ('wisdom.mysticism', 1, 'bulwarkOfTheMire', 1),
    'core:voy': ('logistics.navigation', 1, 'bulwarkOfTheMire', 1),
    'core:andra': ('wisdom.intelligence', 1, 'bulwarkOfTheMire', 1),
    'core:tiva': ('learning.eagleEye', 1, 'bulwarkOfTheMire', 1),
    'core:lacus': ('battlecraft.tactics', 2, 'elementalRebirth', 1),
}

class HeroDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = json.loads((ROOT / 'config/newHorizonsHeroes.json').read_text())

    def test_all_144_starting_packages_are_exact_and_class_legal(self):
        profiles = self.rules['startingSkills']['startingDevelopmentProfiles']
        registry = json.loads((ROOT / 'config/newHorizonsPerks.json').read_text())['skills']
        faction_skills = self.rules['startingSkills']['factionSkills']
        self.assertEqual(len(WORKBOOK_REMAINING_STARTS), 96)
        self.assertEqual(len(ACCEPTED_STARTS), 48)
        self.assertEqual(set(profiles), set(WORKBOOK_REMAINING_STARTS) | set(ACCEPTED_STARTS))
        for faction in ('castle', 'rampart', 'tower', 'inferno', 'necropolis',
                        'dungeon', 'stronghold', 'fortress', 'conflux'):
            roster = json.loads((ROOT / f'config/heroes/{faction}.json').read_text())
            own = faction_skills['core:' + faction]
            for hero, definition in roster.items():
                hero = 'core:' + hero
                with self.subTest(hero=hero):
                    profile = profiles[hero]
                    if hero in WORKBOOK_REMAINING_STARTS:
                        perk = WORKBOOK_REMAINING_STARTS[hero]
                        generic_rank = faction_rank = 1
                        expected_faction = own
                    else:
                        perk, generic_rank, settled_faction, faction_rank = ACCEPTED_STARTS[hero]
                        expected_faction = 'new-horizons:' + settled_faction
                    parent = 'new-horizons:' + perk.split('.')[0]
                    expected_perk = 'new-horizons:' + perk
                    self.assertEqual({row['skill']: row['rank'] for row in profile['skills']},
                                     {parent: generic_rank, expected_faction: faction_rank})
                    self.assertEqual(expected_faction, own)
                    self.assertEqual(len(profile['skills']), 2)
                    self.assertEqual(profile['startingPerks'],
                                     [{'skill': parent, 'perk': expected_perk}])
                    class_swaps = json.loads((ROOT / 'Mods/new-horizons/Content/config/heroes/classSwaps.json').read_text())
                    effective_class = class_swaps.get(hero, {}).get('class', 'core:' + definition['class'])
                    self.assertGreater(self.rules['skillOfferWeights'][effective_class][parent], 0)
                    authored = next(row for row in registry[parent]['perks'] if row['id'] == expected_perk)
                    self.assertEqual(authored['requires'], 'basic')
                    self.assertEqual(authored['effect']['status'], 'active')

    def test_generated_module_captures_all_144_packages_exactly(self):
        module = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
        self.assertEqual(module['settings']['heroes']['newHorizons']['startingSkills'],
                         self.rules['startingSkills'])

    def test_thane_halon_class_swap_is_exact_and_new_horizons_only(self):
        module = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
        swaps = json.loads((ROOT / 'Mods/new-horizons/Content/config/heroes/classSwaps.json').read_text())
        self.assertEqual(swaps, {
            'core:halon': {'class': 'core:alchemist'},
            'core:thane': {'class': 'core:wizard'},
        })
        self.assertEqual(module['heroes'].count('config/heroes/classSwaps.json'), 1)
        legacy = json.loads((ROOT / 'config/heroes/tower.json').read_text())
        self.assertEqual(legacy['halon']['class'], 'wizard')
        self.assertEqual(legacy['thane']['class'], 'alchemist')
        # A class-only override preserves every other prototype field.
        for scoped, override in swaps.items():
            prototype = legacy[scoped.split(':')[1]]
            effective = dict(prototype, **override)
            self.assertEqual({key: value for key, value in effective.items() if key != 'class'},
                             {key: value for key, value in prototype.items() if key != 'class'})

    def test_swapped_classes_use_existing_primary_and_army_capacity_profiles(self):
        capabilities = json.loads((ROOT / 'config/newHorizonsCapabilities.json').read_text())
        swaps = json.loads((ROOT / 'Mods/new-horizons/Content/config/heroes/classSwaps.json').read_text())
        for hero, primary, leadership, caps in (
                ('core:halon', [30, 20, 20, 30], 875, [17, 10, 6]),
                ('core:thane', [5, 5, 45, 45], 650, [13, 8, 4])):
            with self.subTest(hero=hero):
                class_id = swaps[hero]['class']
                self.assertEqual(self.rules['classProfiles'][class_id]['starting'], primary)
                self.assertEqual(capabilities['classProfiles'][class_id]['base'], leadership)
                requirements = capabilities['leadership']['creatureRequirements']
                self.assertEqual([leadership // requirements[creature] for creature in
                                  ('core:gremlin', 'core:stoneGargoyle', 'core:ironGolem')], caps)

    def test_legacy_starting_skill_migration_table_is_canonical_and_honest(self):
        self.assertEqual(self.rules['startingSkills']['legacySkillMigrations'], {
            'core:archery': {'kind': 'skill', 'target': 'new-horizons:archery'},
            'core:armorer': {'kind': 'skill', 'target': 'new-horizons:armorer'},
            'core:artillery': {'kind': 'skill', 'target': 'new-horizons:warMachines'},
            'core:ballistics': {'kind': 'skill', 'target': 'new-horizons:warMachines'},
            'core:diplomacy': {'kind': 'skill', 'target': 'new-horizons:diplomacy'},
            'core:eagleEye': {
                'kind': 'perk', 'target': 'new-horizons:learning.eagleEye', 'fallback': 'remove'},
            'core:estates': {'kind': 'skill', 'target': 'new-horizons:estates'},
            'core:firstAid': {'kind': 'skill', 'target': 'new-horizons:warMachines'},
            'core:intelligence': {
                'kind': 'perk', 'target': 'new-horizons:wisdom.intelligence', 'fallback': 'remove'},
            'core:leadership': {'kind': 'skill', 'target': 'new-horizons:discipline'},
            'core:learning': {'kind': 'skill', 'target': 'new-horizons:learning'},
            'core:logistics': {'kind': 'skill', 'target': 'new-horizons:logistics'},
            'core:luck': {'kind': 'skill', 'target': 'new-horizons:luck'},
            'core:mysticism': {
                'kind': 'perk', 'target': 'new-horizons:wisdom.mysticism', 'fallback': 'remove'},
            'core:navigation': {
                'kind': 'perk', 'target': 'new-horizons:logistics.navigation', 'fallback': 'remove'},
            'core:necromancy': {'kind': 'factionSkill', 'target': 'new-horizons:necromancy'},
            'core:offence': {'kind': 'skill', 'target': 'new-horizons:offense'},
            'core:pathfinding': {
                'kind': 'perk', 'target': 'new-horizons:logistics.pathfinding', 'fallback': 'remove'},
            'core:resistance': {
                'kind': 'perk', 'target': 'new-horizons:warcasting.spellward', 'fallback': 'remove'},
            'core:scholar': {
                'kind': 'perk', 'target': 'new-horizons:learning.scholar', 'fallback': 'remove'},
            'core:scouting': {
                'kind': 'perk', 'target': 'new-horizons:logistics.scouting', 'fallback': 'remove'},
            'core:sorcery': {'kind': 'skill', 'target': 'new-horizons:spellcraft'},
            'core:wisdom': {'kind': 'skill', 'target': 'new-horizons:wisdom'},
            'core:tactics': {
                'kind': 'perk', 'target': 'new-horizons:battlecraft.tactics', 'fallback': 'remove'},
        })

    def test_faction_starting_skill_policy_covers_every_core_faction_and_class(self):
        starting = self.rules['startingSkills']
        faction_skills = starting['factionSkills']
        classes = json.loads((ROOT / 'config/heroClasses.json').read_text())
        self.assertEqual(set(faction_skills), {
            'core:castle', 'core:rampart', 'core:tower', 'core:inferno',
            'core:necropolis', 'core:dungeon', 'core:stronghold',
            'core:fortress', 'core:conflux'})
        for class_id, hero_class in classes.items():
            with self.subTest(hero_class=class_id):
                self.assertIn('core:' + hero_class['faction'], faction_skills)
        self.assertEqual(starting['legacyAliases'],
                         {'core:necropolis': 'core:necromancy'})
        self.assertEqual(starting['magic'], {'replace': 'new-horizons:wisdom'})
        self.assertEqual(starting['might'], {
            'replacePosition': 'second', 'singleSkillFallback': 'append'})

    def test_every_faction_hero_gets_unique_skill_with_role_preserving_replacement(self):
        classes = json.loads((ROOT / 'config/heroClasses.json').read_text())
        starting = self.rules['startingSkills']
        faction_skills = starting['factionSkills']
        legacy_aliases = starting['legacyAliases']
        hero_count = 0

        def scoped(skill):
            return skill if ':' in skill else 'core:' + skill

        for faction in ('castle', 'rampart', 'tower', 'inferno', 'necropolis',
                        'dungeon', 'stronghold', 'fortress', 'conflux'):
            heroes = json.loads((ROOT / f'config/heroes/{faction}.json').read_text())
            for hero_name, hero in heroes.items():
                hero_count += 1
                hero_class = classes[hero['class']]
                self.assertEqual(hero_class['faction'], faction)
                skills = [(scoped(item['skill']), item['level'])
                          for item in hero.get('skills', [])]
                unique = faction_skills['core:' + faction]
                alias = legacy_aliases.get('core:' + faction)

                # Migrate a legacy skill with the same faction identity first.
                if alias and any(skill == alias for skill, _ in skills):
                    alias_ranks = [level for skill, level in skills if skill == alias]
                    converted = []
                    inserted = False
                    for skill, level in skills:
                        if skill == alias:
                            if not inserted:
                                converted.append((unique, max(alias_ranks, key=('basic', 'advanced', 'expert').index)))
                                inserted = True
                        elif skill != unique:
                            converted.append((skill, level))
                    skills = converted

                if hero_class['affinity'] == 'magic':
                    wisdom = [level for skill, level in skills if skill == 'core:wisdom']
                    skills = [(skill, level) for skill, level in skills
                              if skill != 'core:wisdom']
                    if not any(skill == unique for skill, _ in skills):
                        rank = max(wisdom, key=('basic', 'advanced', 'expert').index) if wisdom else 'basic'
                        skills.append((unique, rank))
                    self.assertFalse(any(skill == 'core:wisdom' for skill, _ in skills))
                else:
                    # Wisdom is a Magic-only New Horizons Skill. Rashka and
                    # any future might hero authored with legacy Wisdom keep
                    # the useful generic magical identity as Spellcraft
                    # before their faction Skill occupies the second slot.
                    skills = [('new-horizons:spellcraft' if skill == 'core:wisdom' else skill,
                               level) for skill, level in skills]
                    if not any(skill == unique for skill, _ in skills):
                        if len(skills) > 1:
                            skills[1] = (unique, skills[1][1])
                        else:
                            # A one-skill hero keeps the class/specialty signature;
                            # the faction skill is appended as the explicit fallback.
                            skills.append((unique, 'basic'))

                with self.subTest(hero=hero_name):
                    self.assertIn(unique, {skill for skill, _ in skills})
        self.assertEqual(hero_count, 144)

    def test_all_core_classes_have_exact_canonical_profiles_and_names(self):
        classes = json.loads((ROOT / 'config/heroClasses.json').read_text())
        self.assertEqual(self.rules['classProfiles'], {
            class_id: {'progressionVersion': 3, 'starting': starting,
                       'growth': [value // 5 for value in starting]}
            for class_id, starting in CANONICAL_STARTS.items()
        })
        # Unchanged names continue to come from HCTRAITS; only the four
        # canonical renames are authored as New Horizons translation
        # overrides, so the core class JSON does not replace localization for
        # the other fourteen classes.
        self.assertTrue(all('name' not in hero for hero in classes.values()))
        translations = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())['translations']
        class_patch = json.loads((ROOT / 'Mods/new-horizons/Content/config/heroClasses/names.json').read_text())
        manifest = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
        self.assertIn('config/heroClasses/names.json', manifest['heroClasses'])
        for class_id, expected_name in CANONICAL_CLASS_NAMES.items():
            with self.subTest(hero_class=class_id):
                self.assertEqual(translations['core.heroClass.' + class_id.split(':', 1)[1] + '.name'], expected_name)
                self.assertEqual(class_patch[class_id]['name'], expected_name)

    def test_non_mastery_preview_preserves_battle_mage_translation(self):
        script = ROOT / 'tools/update-new-horizons-module.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            subprocess.run([sys.executable, str(script), '--hero-preview-output', str(output)],
                           check=True, capture_output=True)
            translations = json.loads(output.read_text())['translations']
            self.assertEqual(translations['core.heroClass.alchemist.name'], 'Battle Mage')
            self.assertNotIn('Alchemist', translations.values())

    def test_every_class_has_exact_canonical_skill_offer_weights(self):
        self.assertEqual(set(self.rules['skillOfferWeights']), set(CANONICAL_WEIGHT_ROWS))
        self.assertEqual(len(CANONICAL_SKILLS), 31)
        for class_id, row in CANONICAL_WEIGHT_ROWS.items():
            with self.subTest(hero_class=class_id):
                values = [int(value) for value in row.split()]
                self.assertEqual(len(values), len(CANONICAL_SKILLS))
                self.assertEqual(self.rules['skillOfferWeights'][class_id],
                                 dict(zip(CANONICAL_SKILLS, values)))

    def test_retired_skills_are_excluded_and_only_explicit_skills_grant_growth(self):
        self.assertEqual(set(self.rules['excludedSkills']), {
            'core:airMagic', 'core:earthMagic', 'core:fireMagic', 'core:waterMagic',
            'core:artillery', 'core:ballistics', 'core:firstAid', 'core:eagleEye',
            'core:intelligence', 'core:leadership', 'core:mysticism', 'core:navigation',
            'core:necromancy', 'core:pathfinding', 'core:resistance', 'core:scholar',
            'core:scouting', 'core:sorcery', 'core:tactics', 'core:wisdom',
        })
        self.assertEqual(self.rules['extraGrowth'], [
            {'skill': 'new-horizons:' + skill, 'primary': primary,
             'chances': [0, 10, 20, 30]}
            for skill, primary in [('offense', 0), ('armorer', 1), ('archery', 0),
                                   ('spellcraft', 2), ('wisdom', 3)]
        ])

    def test_rashka_fresh_roster_has_demonic_gating_and_no_legacy_wisdom(self):
        heroes = json.loads((ROOT / 'config/heroes/inferno.json').read_text())
        rashka = heroes['rashka']
        skills = [('new-horizons:spellcraft' if entry['skill'] == 'wisdom'
                   else entry['skill'], entry['level'])
                  for entry in rashka['skills'] if entry['skill'] != 'scholar']
        skills.append(('new-horizons:demonicGating', 'basic'))
        self.assertEqual([skill for skill, _ in skills],
                         ['new-horizons:spellcraft', 'new-horizons:demonicGating'])
        self.assertNotIn('wisdom', [skill for skill, _ in skills])

    def test_preview_generation_is_separate_and_never_overwrites(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        biographies = ROOT / 'Mods/new-horizons/Content/config/heroes/biographies.json'
        biographies_before = biographies.read_bytes()
        script = ROOT / 'tools/update-new-horizons-module.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--hero-preview-output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            generated = json.loads(output.read_text())
            self.assertEqual(generated['settings']['heroes']['newHorizons'], self.rules)
            self.assertEqual(generated['version'], '0.3.0')
            self.assertIn('config/heroes/biographies.json', generated['heroes'])
            self.assertNotIn('newHorizonsCapabilities', generated['settings']['heroes'])
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--hero-preview-output', str(live)], capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertEqual(biographies.read_bytes(), biographies_before)
        self.assertEqual(json.loads(before)['settings']['heroes']['newHorizons'], self.rules)
        subprocess.run([sys.executable, str(script), '--check'], check=True, capture_output=True)

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required for native module drift guard')
    def test_cmake_guard_accepts_activation_and_rejects_rule_drift(self):
        source = (ROOT / 'CMakeLists.txt').read_text()
        block = source.split('# Curated settings are authored once;', 1)[1]
        # This oracle exercises settings/definition drift, not the independent
        # committed-art verification target, which needs its complete package.
        block = ('if(EXISTS "${CMAKE_SOURCE_DIR}/config/newHorizonsCombat.json")\n'
                 + block[block.index('\tset(NH_RULES_FILE'):].split('\nif(ANDROID)', 1)[0])
        with tempfile.TemporaryDirectory(prefix='nh-hero-settings-guard-') as temporary:
            root = Path(temporary)
            (root / 'config').mkdir()
            (root / 'Mods/new-horizons').mkdir(parents=True)
            for name in ('Combat', 'Artifacts', 'Magic', 'CreatureCategories', 'Schools', 'Skills', 'Heroes', 'Capabilities',
                         'Masteries', 'Perks', 'MasteryTexts', 'CreatureCategoryTexts', 'FortTexts', 'MusterTexts',
                         'HeroClassTexts', 'CombatTexts', 'EconomyTexts', 'AdventureSpellTexts', 'ConvenienceBonuses'):
                shutil.copyfile(ROOT / f'config/newHorizons{name}.json', root / f'config/newHorizons{name}.json')
            shutil.copyfile(ROOT / 'Mods/new-horizons/mod.json', root / 'Mods/new-horizons/mod.json')
            script = root / 'check.cmake'
            script.write_text(f'set(CMAKE_SOURCE_DIR "{root.as_posix()}")\n' + block)
            command = ['cmake', '-P', str(script)]
            subprocess.run(command, check=True, capture_output=True)
            for name, key in (('Heroes', 'maxPrimary'), ('Capabilities', 'rulesetVersion'), ('Masteries', 'rulesetVersion')):
                with self.subTest(rules=name):
                    path = root / f'config/newHorizons{name}.json'
                    before = path.read_bytes()
                    rules = json.loads(before)
                    rules[key] += 1
                    path.write_text(json.dumps(rules))
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('Stale curated module settings', result.stderr)
                    path.write_bytes(before)
            for name, field in (('MasteryTexts', 'translations'), ('EconomyTexts', 'translations'),
                                ('ConvenienceBonuses', 'bonuses')):
                with self.subTest(definitions=name):
                    path = root / f'config/newHorizons{name}.json'
                    before = path.read_bytes()
                    data = json.loads(before)
                    data.pop(next(iter(data)))
                    path.write_text(json.dumps(data))
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('Stale curated ' + field, result.stderr)
                    path.write_bytes(before)

            module_path = root / 'Mods/new-horizons/mod.json'
            module = json.loads(module_path.read_bytes())
            module['filesystem'][''][0]['path'] = '/WrongContent'
            module_path.write_text(json.dumps(module))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Stale curated Content root mount', result.stderr)

    def test_scale_and_cap_are_explicit(self):
        self.assertEqual(self.rules['schemaVersion'], 1)
        self.assertEqual(self.rules['rulesetVersion'], 1)
        self.assertTrue(1 <= self.rules['powerDivisor'] <= 1000)
        self.assertTrue(100 <= self.rules['maxPrimary'] <= 1000000)

    def test_canonical_growth_totals_twenty_and_start_is_five_times_growth(self):
        for profile in self.rules['classProfiles'].values():
            self.assertEqual(sum(profile['growth']), 20)
            self.assertEqual(profile['starting'], [5 * value for value in profile['growth']])


if __name__ == '__main__':
    unittest.main()
