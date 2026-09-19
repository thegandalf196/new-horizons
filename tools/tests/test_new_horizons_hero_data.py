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


class HeroDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = json.loads((ROOT / 'config/newHorizonsHeroes.json').read_text())

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

    def test_all_core_classes_have_explicit_provisional_profiles(self):
        classes = json.loads((ROOT / 'config/heroClasses.json').read_text())
        self.assertEqual(set(self.rules['classProfiles']), {'core:' + name for name in classes})
        for profile in self.rules['classProfiles'].values():
            self.assertEqual(sorted(profile['starting']), [5, 10, 15, 20])
            self.assertEqual(len(profile['growth']), 4)
            self.assertTrue(all(isinstance(value, int) and value >= 1 for value in profile['growth']))
            self.assertEqual(sum(profile['growth']), 10)

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
            for name in ('Combat', 'Magic', 'Schools', 'Skills', 'Heroes', 'Capabilities', 'Masteries', 'Perks', 'MasteryTexts', 'ConvenienceBonuses'):
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
