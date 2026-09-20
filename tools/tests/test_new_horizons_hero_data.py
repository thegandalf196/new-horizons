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

CANONICAL_PROFILES = {
    'core:knight': ([15, 20, 5, 10], [3, 4, 1, 2]),
    'core:cleric': ([5, 10, 15, 20], [1, 2, 3, 4]),
    'core:ranger': ([15, 15, 10, 10], [3, 3, 2, 2]),
    'core:druid': ([5, 5, 15, 25], [1, 1, 3, 5]),
    'core:alchemist': ([15, 10, 10, 15], [3, 2, 2, 3]),
    'core:wizard': ([5, 5, 20, 20], [1, 1, 4, 4]),
    'core:demoniac': ([25, 10, 10, 5], [5, 2, 2, 1]),
    'core:heretic': ([10, 5, 20, 15], [2, 1, 4, 3]),
    'core:deathknight': ([20, 10, 15, 5], [4, 2, 3, 1]),
    'core:necromancer': ([5, 10, 20, 15], [1, 2, 4, 3]),
    'core:overlord': ([20, 15, 10, 5], [4, 3, 2, 1]),
    'core:warlock': ([10, 5, 25, 10], [2, 1, 5, 2]),
    'core:barbarian': ([25, 15, 5, 5], [5, 3, 1, 1]),
    'core:battlemage': ([20, 5, 15, 10], [4, 1, 3, 2]),
    'core:beastmaster': ([15, 25, 5, 5], [3, 5, 1, 1]),
    'core:witch': ([5, 10, 10, 25], [1, 2, 2, 5]),
    'core:planeswalker': ([15, 10, 15, 10], [3, 2, 3, 2]),
    'core:elementalist': ([5, 5, 25, 15], [1, 1, 5, 3]),
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


class HeroDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = json.loads((ROOT / 'config/newHorizonsHeroes.json').read_text())

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
        self.assertEqual(starting['magic'], {'replace': 'core:wisdom'})
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
                elif not any(skill == unique for skill, _ in skills):
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
            class_id: {'starting': starting, 'growth': growth}
            for class_id, (starting, growth) in CANONICAL_PROFILES.items()
        })
        # Unchanged names continue to come from HCTRAITS; only the four
        # canonical renames are authored as New Horizons translation
        # overrides, so the core class JSON does not replace localization for
        # the other fourteen classes.
        self.assertTrue(all('name' not in hero for hero in classes.values()))
        translations = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())['translations']
        for class_id, expected_name in CANONICAL_CLASS_NAMES.items():
            with self.subTest(hero_class=class_id):
                self.assertEqual(translations['core.heroClass.' + class_id.split(':', 1)[1] + '.name'], expected_name)

    def test_every_class_has_exact_canonical_skill_offer_weights(self):
        self.assertEqual(set(self.rules['skillOfferWeights']), set(CANONICAL_WEIGHT_ROWS))
        self.assertEqual(len(CANONICAL_SKILLS), 31)
        for class_id, row in CANONICAL_WEIGHT_ROWS.items():
            with self.subTest(hero_class=class_id):
                values = [int(value) for value in row.split()]
                self.assertEqual(len(values), len(CANONICAL_SKILLS))
                self.assertEqual(self.rules['skillOfferWeights'][class_id],
                                 dict(zip(CANONICAL_SKILLS, values)))

    def test_retired_skills_are_excluded_and_extra_growth_uses_canonical_skills(self):
        self.assertEqual(set(self.rules['excludedSkills']), {
            'core:airMagic', 'core:earthMagic', 'core:fireMagic', 'core:waterMagic',
            'core:artillery', 'core:ballistics', 'core:firstAid', 'core:eagleEye',
            'core:intelligence', 'core:leadership', 'core:mysticism', 'core:navigation',
            'core:necromancy', 'core:pathfinding', 'core:resistance', 'core:scholar',
            'core:scouting', 'core:sorcery', 'core:tactics', 'core:wisdom',
        })
        self.assertEqual(self.rules['extraGrowth'], [
            {'skill': 'new-horizons:offense', 'primary': 0, 'chances': [0, 10, 20, 30]},
            {'skill': 'new-horizons:armorer', 'primary': 1, 'chances': [0, 10, 20, 30]},
            {'skill': 'new-horizons:spellcraft', 'primary': 2, 'chances': [0, 10, 20, 30]},
            {'skill': 'new-horizons:wisdom', 'primary': 3, 'chances': [0, 10, 20, 30]},
        ])

    def test_preview_generation_is_separate_and_never_overwrites(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        script = ROOT / 'tools/update-new-horizons-module.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--hero-preview-output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            generated = json.loads(output.read_text())
            self.assertEqual(generated['settings']['heroes']['newHorizons'], self.rules)
            self.assertEqual(generated['version'], '0.3.0')
            self.assertNotIn('newHorizonsCapabilities', generated['settings']['heroes'])
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--hero-preview-output', str(live)], capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertEqual(json.loads(before)['settings']['heroes']['newHorizons'], self.rules)
        subprocess.run([sys.executable, str(script), '--check'], check=True, capture_output=True)

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required for native module drift guard')
    def test_cmake_guard_accepts_activation_and_rejects_rule_drift(self):
        source = (ROOT / 'CMakeLists.txt').read_text()
        block = source.split('# Curated settings are authored once;', 1)[1]
        block = block[block.index('if(EXISTS'):].split('\nif(ANDROID)', 1)[0]
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            root = Path(temporary)
            (root / 'config').mkdir()
            (root / 'Mods/new-horizons').mkdir(parents=True)
            for name in ('Combat', 'Magic', 'Schools', 'Skills', 'Heroes', 'Capabilities', 'Masteries', 'Perks', 'MasteryTexts', 'HeroClassTexts', 'ConvenienceBonuses'):
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
            for name, field in (('MasteryTexts', 'translations'), ('ConvenienceBonuses', 'bonuses')):
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

    def test_independent_extras_use_owned_skill_ranks_only(self):
        seen = set()
        for extra in self.rules['extraGrowth']:
            self.assertTrue(extra['skill'].startswith(('core:', 'new-horizons:')))
            self.assertNotIn(extra['skill'], seen)
            seen.add(extra['skill'])
            self.assertIn(extra['primary'], range(4))
            self.assertEqual(len(extra['chances']), 4)
            self.assertEqual(extra['chances'][0], 0)
            self.assertTrue(all(isinstance(value, int) and 0 <= value <= 100 for value in extra['chances']))


if __name__ == '__main__':
    unittest.main()
