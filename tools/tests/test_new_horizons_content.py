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
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parents[2]
SCHOOLS = ('light', 'nature', 'sorcery', 'havoc', 'shadow', 'chaos')
RANKS = ('basic', 'advanced', 'expert')
NEW_HORIZONS_SPELLS = {
    'new-horizons:counterspell',
    'new-horizons:disintegrate',
    'new-horizons:timeStop',
    'new-horizons:transfigureMatter',
}
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


def legacy_rules(rules):
    """Return the same snapshot in the saved-compatible v1 shape."""
    result = copy.deepcopy(rules)
    if result:
        result['rulesetVersion'] = 1
        for spell in result['spells'].values():
            spell.pop('directDamage', None)
    return result


def magic_validators():
    v1 = load('config/schemas/newHorizonsMagic.json')
    v2 = load('config/schemas/newHorizonsMagicV2.json')
    registry = Registry().with_resources([
        ('vcmi:newHorizonsMagic', Resource.from_contents(v1)),
        ('vcmi:newHorizonsMagicV2', Resource.from_contents(v2)),
    ])
    return Draft4Validator(v1, registry=registry), Draft4Validator(v2, registry=registry)


def validate_rules(rules):
    v1, v2 = magic_validators()
    (v2 if rules.get('rulesetVersion') == 2 else v1).validate(rules)
    if not rules:
        return
    if set(rules['spells']) != common_spells() | NEW_HORIZONS_SPELLS:
        raise ValueError('Hero spell inventory does not match curated mappings')
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

    def test_orders_keyboard_binding(self):
        bindings = load('config/keyBindingsConfig.json')
        self.assertEqual(bindings['keyboard']['battleOpenOrders'], 'B')
        self.assertEqual(bindings['keyboard']['battleCastSpell'], 'C')
        for device, keys in bindings.items():
            if device != 'keyboard':
                self.assertNotIn('battleOpenOrders', keys)

    def test_orders_binding_label(self):
        texts = load('Mods/vcmi/Content/config/translations/english.json')
        self.assertEqual(texts['vcmi.keyBindings.keyBinding.battleOpenOrders'],
                         'Battle open Orders and Doctrines')

    def test_new_horizons_combat_emits_orders_only(self):
        combat = load('config/newHorizonsCombat.json')['combat']['heroCommands']
        Draft4Validator(load('config/schemas/newHorizonsCombatV3.json')).validate(combat)
        self.assertEqual(combat['rulesetVersion'], 3)
        self.assertEqual(set(combat['commands']), {
            'charge', 'holdTheLine', 'focusFire', 'riposte', 'brace',
            'protect', 'flank', 'secondWind',
        })
        self.assertNotIn('advance', combat['commands'])
        self.assertNotIn('aggressive', combat['commands'])
        self.assertNotIn('defensive', combat['commands'])

    def test_orders_shortcut_registered_once(self):
        handler = (ROOT / 'client/gui/ShortcutHandler.cpp').read_text()
        self.assertEqual(handler.count('{"battleOpenOrders",'), 1)
        shortcuts = (ROOT / 'client/gui/Shortcut.h').read_text()
        self.assertRegex(shortcuts,
                         r'LIST_TOWN_BOTTOM,[\s\S]*BATTLE_OPEN_ORDERS,\s*AFTER_LAST')

    def test_complete_existing_spell_inventory_and_legacy_schema(self):
        self.assertEqual(len(common_spells()), 69)
        self.assertEqual(set(self.rules['spells']) - common_spells(),
                         NEW_HORIZONS_SPELLS)
        validate_rules({})
        validate_rules(legacy_rules(self.rules))
        validate_rules(self.rules)

    def test_active_v2_direct_damage_formulas_match_canonical_spell_families(self):
        arrow = self.rules['spells']['core:magicArrow']
        self.assertEqual(arrow['schools'], ['new-horizons:sorcery'])
        self.assertEqual(arrow['level'], 1)
        self.assertEqual(arrow['directDamage'], {'base': 20, 'powerCoefficient': 20})
        self.assertEqual({name for name, spell in self.rules['spells'].items()
                          if 'directDamage' in spell}, {
                              'core:armageddon',
                              'core:chainLightning',
                              'core:magicArrow',
                              'core:fireball',
                              'core:fireWall',
                              'core:frostRing',
                              'core:iceBolt',
                              'core:inferno',
                              'core:landMine',
                              'core:lightningBolt',
                              'core:meteorShower',
                              'new-horizons:disintegrate',
                          })
        self.assertEqual(self.rules['spells']['core:fireball']['directDamage'],
                         {'base': 25, 'powerCoefficient': 8})
        self.assertEqual(self.rules['spells']['core:fireWall']['directDamage'],
                         {'base': 40, 'powerCoefficient': 10})
        self.assertEqual(self.rules['spells']['core:iceBolt']['directDamage'],
                         {'base': 45, 'powerCoefficient': 10})
        self.assertEqual(self.rules['spells']['core:lightningBolt']['directDamage'],
                         {'base': 20, 'powerCoefficient': 15})
        self.assertEqual(self.rules['spells']['core:frostRing']['directDamage'],
                         {'base': 55, 'powerCoefficient': 11})
        self.assertEqual(self.rules['spells']['core:inferno']['directDamage'],
                         {'base': 70, 'powerCoefficient': 12})
        self.assertEqual(self.rules['spells']['core:landMine']['directDamage'],
                         {'base': 60, 'powerCoefficient': 11})
        self.assertEqual(self.rules['spells']['core:chainLightning']['directDamage'],
                         {'base': 130, 'powerCoefficient': 18})
        self.assertEqual(self.rules['spells']['core:meteorShower']['directDamage'],
                         {'base': 110, 'powerCoefficient': 15})
        self.assertEqual(self.rules['spells']['core:armageddon']['directDamage'],
                         {'base': 150, 'powerCoefficient': 18})
        self.assertEqual(self.rules['spells']['new-horizons:disintegrate']['directDamage'],
                         {'base': 180, 'powerCoefficient': 25})
        self.assertNotIn('new-horizons:magicMissile', self.rules['spells'])

    def test_disintegrate_content_uses_authoritative_destroy_remains_effect(self):
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['disintegrate']
        self.assertEqual(spell['level'], 5)
        self.assertEqual(spell['school'], {'new-horizons:havoc': True})
        for rank in ('none', 'basic', 'advanced', 'expert'):
            level = spell['levels'][rank]
            self.assertEqual(level['cost'], 25)
            self.assertEqual(level['battleEffects']['directDamage'],
                             {'type': 'damage', 'destroyRemains': True})

    def test_sorcery_spell_foundation_definitions_remain_deferred(self):
        """Deferred source definitions stay schema-shaped but out of the saved roster."""
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        expected = {
            'phantomArmy': (4, 15, 'phantomArmy'),
            'spellLock': (5, 22, 'spellLock'),
        }
        for name, (level, cost, effect) in expected.items():
            with self.subTest(spell=name):
                spell = content[name]
                self.assertEqual(spell['school'], {'new-horizons:sorcery': True})
                self.assertEqual(spell['level'], level)
                self.assertTrue(spell['flags']['special'])
                self.assertNotIn('new-horizons:' + name, self.rules['spells'])
                self.assertEqual(set(spell['levels']), {'none', 'basic', 'advanced', 'expert'})
                for rank in ('none', 'basic', 'advanced', 'expert'):
                    current = spell['levels'][rank]
                    self.assertEqual(current['cost'], cost)
                    self.assertEqual(current['battleEffects'][effect]['type'],
                                     'core:' + effect)

    def test_time_stop_is_an_active_sorcery_spell(self):
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        spell = content['timeStop']
        self.assertEqual(spell['school'], {'new-horizons:sorcery': True})
        self.assertEqual(spell['level'], 5)
        self.assertFalse(spell['flags'].get('special', False))
        self.assertEqual(self.rules['spells']['new-horizons:timeStop'], {
            'schools': ['new-horizons:sorcery'],
            'level': 5,
            'costs': [23, 23, 23, 23],
        })
        for rank in ('none', 'basic', 'advanced', 'expert'):
            current = spell['levels'][rank]
            self.assertEqual(current['cost'], 23)
            self.assertEqual(current['battleEffects']['timeStop']['type'],
                             'core:timeStop')

    def test_sorcery_effect_foundation_scripts_are_registered_without_clone_reuse(self):
        """Registration is a schema/content check, not proof of authoritative runtime behavior."""
        scripts = load('config/scriptsSpells.json')
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        for name in ('phantomArmy', 'timeStop', 'spellLock'):
            with self.subTest(effect=name):
                self.assertEqual(scripts[name]['implements'], 'spellEffect')
                self.assertEqual(scripts[name]['script'], 'spells/' + name)
                self.assertTrue((ROOT / 'scripts/spells' / (name + '.lua')).is_file())
        self.assertEqual(content['phantomArmy']['levels']['none']['battleEffects']
                         ['phantomArmy']['type'], 'core:phantomArmy')
        phantom_source = (ROOT / 'scripts/spells/phantomArmy.lua').read_text(encoding='utf-8')
        self.assertNotIn('require("spells/clone")', phantom_source)
        self.assertNotIn('setCloned(', phantom_source)

    def test_generated_module_matches_all_canonical_data(self):
        module = load('Mods/new-horizons/mod.json')
        settings = load('config/newHorizonsCombat.json')
        settings['magic'] = {'newHorizons': self.rules}
        settings['heroes'] = {'newHorizons': load('config/newHorizonsHeroes.json'),
                              'newHorizonsCapabilities': load('config/newHorizonsCapabilities.json'),
                              'newHorizonsMasteries': load('config/newHorizonsMasteries.json'),
                              'newHorizonsPerks': load('config/newHorizonsPerks.json')}
        self.assertEqual(module['settings'], settings)
        self.assertEqual(module['version'], '0.7.0')
        self.assertIn('Magic Arrow', module['description'])
        self.assertIn('Overcharge', module['description'])
        self.assertEqual(module['spellSchools'], load('config/newHorizonsSchools.json'))
        self.assertEqual(module['skills'], load('config/newHorizonsSkills.json'))
        self.assertEqual(module['filesystem']['SPRITES/'], [{'type': 'dir', 'path': '/Images'}])
        translations = load('config/newHorizonsMasteryTexts.json')
        translations.update(load('config/newHorizonsHeroClassTexts.json'))
        self.assertEqual(module['translations'], translations)
        self.assertEqual(module['bonuses'], load('config/newHorizonsConvenienceBonuses.json'))
        self.assertEqual(module['filesystem'][''], [{'type': 'dir', 'path': '/Content'}])
        self.assertFalse(module['keepDisabled'])
        magic_schema = load('config/schemas/gameSettings.json')['properties']['magic']['properties']['newHorizons']
        self.assertEqual(magic_schema['anyOf'], [
            {'$ref': 'newHorizonsMagic.json'},
            {'$ref': 'newHorizonsMagicV2.json'},
        ])
        self.assertEqual(load('config/gameConfig.json')['settings']['magic']['newHorizons'], {})

    def test_registered_school_skill_graph_and_real_rank_images(self):
        schools = load('config/newHorizonsSchools.json')
        skills = load('config/newHorizonsSkills.json')
        perk_skills = load('config/newHorizonsPerks.json')['skills']
        self.assertEqual(set(schools), set(SCHOOLS))
        self.assertEqual({'new-horizons:' + key for key in skills}, set(perk_skills))
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
        self.assertIn('new-horizons:timeStop', self.rules['spells'])

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
