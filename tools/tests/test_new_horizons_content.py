#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline curated data checks, not native loading, rendering or gameplay proof.

Requires jsonschema. No purchaser assets, game executable or compiler is used.
"""
import copy
import json
from pathlib import Path
import re
import struct
import unittest
import zlib

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
SCHOOLS = ('light', 'nature', 'sorcery', 'havoc', 'shadow', 'chaos')
RANKS = ('basic', 'advanced', 'expert')
SIZES = {'small': (32, 32), 'medium': (44, 44),
         'large': (82, 93), 'scenarioBonus': (58, 64)}
STRING = r'"(?:\\.|[^"\\])*"'


def parse_jsonc(text):
    # Preserve quoted tokens, including URL/comment-looking text and escapes.
    text = re.sub(STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda m: m[0] if m[0].startswith('"') else '', text)
    text = re.sub(STRING + r'|,(?=\s*[}\]])',
                  lambda m: m[0] if m[0].startswith('"') else '', text)
    return json.loads(text)


def load(name):
    return parse_jsonc((ROOT / name).read_text(encoding='utf-8'))


def common_spells():
    result = set()
    for filename in ('adventure', 'offensive', 'other', 'timed'):
        for key, definition in load(f'config/spells/{filename}.json').items():
            # The original SPTRAITS section has 70 hero slots. Titan's Bolt is
            # special; trigger definitions are explicitly special/non-indexed.
            if (0 <= definition.get('index', -1) < 70
                    and not definition.get('flags', {}).get('special', False)):
                result.add('core:' + key)
    return result


def validate_rules(rules):
    Draft4Validator(load('config/schemas/newHorizonsMagic.json')).validate(rules)
    if not rules:
        return
    if set(rules['spells']) != common_spells():
        raise ValueError('Common hero spell inventory does not match curated mappings')
    pairs = set()
    for entry in rules.get('factions', {}).values():
        pair = frozenset((entry['major'], entry['minor']))
        if len(pair) != 2 or pair in pairs:
            raise ValueError('Faction schools must form distinct, unique pairs')
        pairs.add(pair)


def png_size(path):
    header = path.read_bytes()[:33]
    if (len(header) != 33 or header[:8] != b'\x89PNG\r\n\x1a\n'
            or header[8:16] != b'\0\0\0\rIHDR'
            or zlib.crc32(header[12:29]) != struct.unpack('>I', header[29:33])[0]):
        raise ValueError('Invalid PNG IHDR')
    width, height, depth, color = struct.unpack('>IIBB', header[16:26])
    if (depth, color) != (8, 6):
        raise ValueError('Expected authored eight-bit RGBA artwork')
    return width, height


class NewHorizonsContentTest(unittest.TestCase):
    def setUp(self):
        self.rules = load('config/newHorizonsMagic.json')

    def test_complete_existing_spell_inventory_and_legacy_schema(self):
        self.assertEqual(len(common_spells()), 69)
        validate_rules({})
        validate_rules(self.rules)

    def test_generated_module_matches_all_canonical_data(self):
        module = load('Mods/new-horizons/mod.json')
        settings = load('config/newHorizonsCombat.json')
        settings['magic'] = {'newHorizons': self.rules}
        settings['heroes'] = {'newHorizons': load('config/newHorizonsHeroes.json')}
        self.assertEqual(module['settings'], settings)
        self.assertEqual(module['spellSchools'], load('config/newHorizonsSchools.json'))
        self.assertEqual(module['skills'], load('config/newHorizonsSkills.json'))
        self.assertEqual(module['filesystem']['SPRITES/'], [{'type': 'dir', 'path': '/Images'}])
        self.assertFalse(module['keepDisabled'])
        self.assertEqual(load('config/gameConfig.json')['settings']['magic']['newHorizons'], {})

    def test_registered_school_skill_graph_and_real_rank_images(self):
        schools = load('config/newHorizonsSchools.json')
        skills = load('config/newHorizonsSkills.json')
        self.assertEqual(set(schools), set(SCHOOLS))
        self.assertEqual(set(skills), {s + 'Magic' for s in SCHOOLS})
        self.assertEqual(self.rules['schools'], ['new-horizons:' + s for s in SCHOOLS])
        for name, school in schools.items():
            with self.subTest(school=name):
                Draft4Validator(load('config/schemas/spellSchool.json')).validate(school)
                self.assertNotIn('index', school)
                for field in ('schoolHeader', 'schoolBookmark'):
                    self.assertTrue((ROOT / 'Mods/new-horizons/Images' / school[field]).is_file())
                skill = skills[name + 'Magic']
                self.assertNotIn('index', skill)
                self.assertEqual(skill['gainChance'], {'might': 2, 'magic': 6})
                for value, rank in enumerate(RANKS, 1):
                    effect = skill[rank]['effects']['schoolMastery']
                    self.assertEqual(effect, {'type': 'MAGIC_SCHOOL_SKILL',
                                             'subtype': 'new-horizons:' + name,
                                             'valueType': 'BASE_NUMBER', 'val': value})
                    for size, dimensions in SIZES.items():
                        image = skill[rank]['images'][size]
                        self.assertEqual(image, f'NH_{name}Magic_{rank}_{size}.png')
                        self.assertEqual(png_size(ROOT / 'Mods/new-horizons/Images' / image), dimensions)
        legacy = load('config/spellSchools.json')
        self.assertEqual({k: v['index'] for k, v in legacy.items()},
                         {'air': 0, 'fire': 1, 'earth': 2, 'water': 3})

    def test_conflicts_remain_explicit_and_no_fictional_new_spells(self):
        self.assertTrue(self.rules['factions']['core:necropolis']['provisional'])
        self.assertTrue(self.rules['factions']['core:fortress']['provisional'])
        self.assertEqual(set(self.rules['spells']['core:blind']['schools']),
                         {'new-horizons:shadow', 'new-horizons:chaos'})
        self.assertNotIn('core:titanBolt', self.rules['spells'])
        self.assertNotIn('core:poison', self.rules['spells'])
        self.assertNotIn('new-horizons:timeStop', self.rules['spells'])

    def test_negative_controls(self):
        corruptions = [
            lambda r: r['schools'].__setitem__(1, r['schools'][0]),
            lambda r: r['spells'].pop('core:haste'),
            lambda r: r['spells'].__setitem__('core:poison', {'schools': [r['schools'][0]]}),
            lambda r: r['spells']['core:haste'].__setitem__('costs', [1, 2, 3]),
            lambda r: r['spells']['core:haste'].__setitem__('costs', [True, 2, 3, 4]),
            lambda r: r['spells']['core:haste'].__setitem__('level', 6),
            lambda r: r['spells']['core:haste'].__setitem__('schools', ['core:air']),
            lambda r: r['spells']['core:haste'].pop('schools'),
            lambda r: r['schoolSkills'].pop('new-horizons:light'),
            lambda r: r['skillReplacements'].__setitem__('core:airMagic', 'new-horizons:chaosMagic'),
            lambda r: r['factionWeights'].__setitem__('major', 0),
            lambda r: r['factions']['core:castle'].__setitem__('minor', 'new-horizons:light'),
            lambda r: r.__setitem__('unexpected', True),
        ]
        for index, corrupt in enumerate(corruptions):
            with self.subTest(corruption=index):
                rules = copy.deepcopy(self.rules)
                corrupt(rules)
                with self.assertRaises(Exception):
                    validate_rules(rules)

    def test_jsonc_preserves_strings(self):
        self.assertEqual(parse_jsonc('{"url":"https://example.test/a/*b*/", // c\n "x":[1,],}'),
                         {'url': 'https://example.test/a/*b*/', 'x': [1]})


if __name__ == '__main__':
    unittest.main()
