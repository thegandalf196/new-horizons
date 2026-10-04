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

from jsonschema import Draft4Validator, ValidationError
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parents[2]
SCHOOLS = ('light', 'nature', 'sorcery', 'havoc', 'shadow', 'chaos')
RANKS = ('basic', 'advanced', 'expert')
NEW_HORIZONS_SPELLS = {
    'new-horizons:shieldOfChaos',
    'new-horizons:blink',
    'new-horizons:hydrasVitality',
    'new-horizons:verdantPrison',
    'new-horizons:summonTrolls',
    'new-horizons:vengefulVines',
    'new-horizons:entangle',
    'new-horizons:crusade',
    'new-horizons:divineRetribution',
    'new-horizons:purify',
    'new-horizons:focusMagic',
    'new-horizons:frailty',
    'new-horizons:guardianSpirit',
    'new-horizons:heavenlyGale',
    'new-horizons:hexOfPain',
    'new-horizons:holyArmor',
    'new-horizons:holyWrath',
    'new-horizons:handOfFate',
    'new-horizons:lifeDrain',
    'new-horizons:counterspell',
    'new-horizons:disintegrate',
    'new-horizons:doom',
    'new-horizons:masterChainLightning',
    'new-horizons:phantomArmy',
    'new-horizons:plague',
    'new-horizons:poison',
    'new-horizons:regeneration',
    'new-horizons:sanctuary',
    'new-horizons:reanimate',
    'new-horizons:shadowGift',
    'new-horizons:soulChain',
    'new-horizons:soulReaper',
    'new-horizons:vampirism',
    'new-horizons:spellLock',
    'new-horizons:stormOfDaggers',
    'new-horizons:timeStop',
    'new-horizons:transfigureMatter',
}
INACTIVE_CORE_SPELLS = {'core:animateDead'}
ADVENTURE_SPELLS = {
    'core:summonBoat': (1, 20),
    'core:waterWalk': (2, 30),
    'core:townPortal': (3, 50),
    'core:fly': (4, 60),
    'core:dimensionDoor': (5, 80),
}
NH_FACTION_SPECIALTY_PRESENTATIONS = {
    'core:sanya': ('Divine Mandate', 'divineMandate'),
    'core:geon': ('Shroud of Malassa', 'shroudOfMalassa'),
    'core:tiva': ('Bulwark of the Mire', 'bulwarkOfTheMire'),
    'core:nimbus': ('Necromancy', 'necromancy'),
    'core:malcom': ('Sylvan Luck', 'sylvanLuck'),
    'core:oris': ('Bloodrage', 'bloodrage'),
    'core:serena': ('Metamagic', 'metamagic'),
    'core:rion': ('Divine Mandate', 'divineMandate'),
    'core:verdish': ('Bulwark of the Mire', 'bulwarkOfTheMire'),
    'core:gem': ('Sylvan Luck', 'sylvanLuck'),
    'core:andra': ('Bulwark of the Mire', 'bulwarkOfTheMire'),
    'core:ayden': ('Demonic Gating', 'demonicGating'),
    'core:elleshar': ('Sylvan Luck', 'sylvanLuck'),
    'core:jaegar': ('Shroud of Malassa', 'shroudOfMalassa'),
    'core:rosic': ('Bulwark of the Mire', 'bulwarkOfTheMire'),
    'core:axsis': ('Demonic Gating', 'demonicGating'),
    'core:isra': ('Necromancy', 'necromancy'),
    'core:vidomina': ('Necromancy', 'necromancy'),
    'core:thorgrim': ('Sylvan Luck', 'sylvanLuck'),
    'core:malekith': ('Shroud of Malassa', 'shroudOfMalassa'),
    'core:styg': ('Bulwark of the Mire', 'bulwarkOfTheMire'),
    'core:zydar': ('Demonic Gating', 'demonicGating'),
    'core:sandro': ('Necromancy', 'necromancy'),
    'core:gird': ('Bloodrage', 'bloodrage'),
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
        result.pop('warcasting', None)
        result.pop('spellPoints', None)
        result.pop('mageGuildGeneration', None)
        result.pop('physicalDamageReductionCapPercent', None)
        result.pop('schoolRankPowerCoefficientPercent', None)
        result.pop('spellcraftEfficiencyPercent', None)
        for faction in result.get('factions', {}).values():
            faction['major'] = faction.pop('preferredA')
            faction['minor'] = faction.pop('preferredB')
        for spell in result['spells'].values():
            spell.pop('directDamage', None)
            spell.pop('active', None)
            spell.pop('cureAfflictions', None)
            spell.pop('selectedPlacement', None)
            spell.pop('earthquake', None)
            spell.pop('structures', None)
    return result


def magic_validators():
    v1 = load('config/schemas/newHorizonsMagic.json')
    v2 = load('config/schemas/newHorizonsMagicV2.json')
    v3 = load('config/schemas/newHorizonsMagicV3.json')
    registry = Registry().with_resources([
        ('vcmi:newHorizonsMagic', Resource.from_contents(v1)),
        ('vcmi:newHorizonsMagicV2', Resource.from_contents(v2)),
        ('vcmi:newHorizonsMagicV3', Resource.from_contents(v3)),
    ])
    return (Draft4Validator(v1, registry=registry), Draft4Validator(v2, registry=registry),
            Draft4Validator(v3, registry=registry))


def validate_rules(rules):
    v1, v2, v3 = magic_validators()
    validator = {2: v2, 3: v3}.get(rules.get('rulesetVersion'), v1)
    validator.validate(rules)
    if not rules:
        return
    if (set(rules['spells']) | set(rules.get('adventureSpells', {}))
            != common_spells() | NEW_HORIZONS_SPELLS):
        raise ValueError('Hero spell inventory does not match curated mappings')
    pairs = set()
    for entry in rules.get('factions', {}).values():
        fixed = 'mageGuildGeneration' in rules
        pair = frozenset((entry['preferredA' if fixed else 'major'],
                          entry['preferredB' if fixed else 'minor']))
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

    def test_nature_poison_is_a_distinct_hero_spell_with_provisional_art(self):
        spell_id = 'new-horizons:poison'
        row = self.rules['spells'][spell_id]
        self.assertEqual(row['schools'], ['new-horizons:nature'])
        self.assertEqual(row['level'], 2)
        self.assertEqual(row['costs'], [7, 7, 7, 7])
        self.assertNotIn('core:poison', self.rules['spells'])
        self.assertIn('core:poison', self.rules['spells']['core:cure']['cureAfflictions'])

        definitions = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        self.assertNotIn('core:poison', definitions)
        spell = definitions['poison']
        self.assertEqual(spell['name'], 'Poison')
        self.assertEqual(spell['school'], {'new-horizons:nature': True})
        self.assertEqual(spell['level'], 2)
        self.assertEqual(spell['targetType'], 'CREATURE')
        self.assertEqual(set(spell['levels']), {'none', 'basic', 'advanced', 'expert'})
        for rank, level in spell['levels'].items():
            with self.subTest(rank=rank):
                self.assertEqual(level['range'], '0')
                self.assertEqual(level['cost'], 7)
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            with self.subTest(role=role):
                self.assertTrue(filename.startswith('NH_nature_poison_'))
                self.assertEqual(struct.unpack('>II',
                    (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                    (size, size))

    def test_hex_of_pain_has_roster_effect_and_purpose_made_art(self):
        row = self.rules['spells']['new-horizons:hexOfPain']
        self.assertEqual(row['schools'], ['new-horizons:shadow'])
        self.assertEqual((row['level'], row['costs']), (2, [8, 8, 8, 8]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['hexOfPain']
        self.assertEqual((spell['name'], spell['targetType']), ('Hex of Pain', 'CREATURE'))
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 8)
            self.assertEqual(level['battleEffects']['hexOfPain']['type'],
                             'core:hexOfPainEffect')
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_hex_of_pain_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))

    def test_frailty_has_roster_effect_and_purpose_made_art(self):
        row = self.rules['spells']['new-horizons:frailty']
        self.assertEqual(row['schools'], ['new-horizons:shadow'])
        self.assertEqual((row['level'], row['costs']), (2, [8, 8, 8, 8]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['frailty']
        self.assertEqual((spell['name'], spell['targetType']), ('Frailty', 'CREATURE'))
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 8)
            self.assertEqual(level['battleEffects']['frailty']['type'],
                             'core:frailtyEffect')
        for role, size in (('iconBook', 44), ('iconScroll', 44),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_frailty_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))

    def test_soul_chain_has_roster_effect_and_purpose_made_art(self):
        row = self.rules['spells']['new-horizons:soulChain']
        self.assertEqual(row['schools'], ['new-horizons:shadow'])
        self.assertEqual((row['level'], row['costs']), (3, [12, 12, 12, 12]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['soulChain']
        self.assertEqual((spell['name'], spell['targetType']), ('Soul Chain', 'CREATURE'))
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 12)
            self.assertEqual(level['battleEffects']['soulChain']['type'],
                             'core:soulChainEffect')
        for role, size in (('iconBook', 44), ('iconScroll', 44),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_soul_chain_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))

    def test_shadow_gift_has_roster_effect_dark_gift_and_purpose_made_art(self):
        row = self.rules['spells']['new-horizons:shadowGift']
        self.assertEqual(row['schools'], ['new-horizons:shadow'])
        self.assertEqual((row['level'], row['costs']), (3, [12, 12, 12, 12]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['shadowGift']
        self.assertEqual((spell['name'], spell['targetType']), ('Shadow Gift', 'CREATURE'))
        self.assertTrue(spell['flags']['positive'])
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 12)
            self.assertEqual(level['battleEffects']['shadowGift']['type'],
                             'core:shadowGiftEffect')
        for role, size in (('iconBook', 44), ('iconScroll', 44),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_shadow_gift_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))
        shadow_perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:shadowMagic']['perks']
        dark_gift = next(perk for perk in shadow_perks
                         if perk['id'] == 'new-horizons:shadowMagic.darkGift')
        self.assertEqual(dark_gift['effect']['status'], 'active')

    def test_vampirism_has_roster_effect_night_feeder_and_purpose_made_art(self):
        row = self.rules['spells']['new-horizons:vampirism']
        self.assertEqual(row['schools'], ['new-horizons:shadow'])
        self.assertEqual((row['level'], row['costs']), (4, [15, 15, 15, 15]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['vampirism']
        self.assertEqual((spell['name'], spell['targetType']), ('Vampirism', 'CREATURE'))
        self.assertTrue(spell['flags']['positive'])
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 15)
            self.assertEqual(level['battleEffects']['vampirism']['type'],
                             'core:vampirismEffect')
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertIn('vampirism', filename)
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))
        shadow_perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:shadowMagic']['perks']
        night_feeder = next(perk for perk in shadow_perks
                            if perk['id'] == 'new-horizons:shadowMagic.nightFeeder')
        self.assertEqual(night_feeder['effect']['status'], 'active')
        scripts = load('config/scriptsSpells.json')
        combat = load('config/scriptsCombat.json')
        self.assertEqual(scripts['vampirismEffect']['script'], 'spells/vampirism')
        self.assertEqual(combat['vampirism']['script'], 'combat/vampirism')
        texts = load('config/newHorizonsCombatTexts.json')
        self.assertIn('new-horizons.combat.vampirism.healed', texts)

    def test_reanimate_replaces_legacy_animate_dead_in_v3(self):
        row = self.rules['spells']['new-horizons:reanimate']
        self.assertEqual(row['schools'], ['new-horizons:shadow'])
        self.assertEqual((row['level'], row['costs']), (4, [16, 16, 16, 16]))
        self.assertFalse(self.rules['spells']['core:animateDead']['active'])
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['reanimate']
        self.assertEqual((spell['name'], spell['targetType']), ('Re-animate', 'CREATURE'))
        self.assertTrue(spell['flags']['rising'])
        self.assertTrue(spell['flags']['positive'])
        self.assertEqual(spell['targetCondition'], {})
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 16)
            self.assertEqual(level['battleEffects']['reanimate']['type'],
                             'core:reanimateEffect')
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_reanimate_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))
        shadow_perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:shadowMagic']['perks']
        reanimator = next(perk for perk in shadow_perks
                          if perk['id'] == 'new-horizons:shadowMagic.reanimator')
        self.assertEqual(reanimator['effect']['status'], 'active')
        scripts = load('config/scriptsSpells.json')
        self.assertEqual(scripts['reanimateEffect']['script'], 'spells/reanimate')
        texts = load('config/newHorizonsCombatTexts.json')
        self.assertIn('new-horizons.combat.reanimate.restored', texts)
        self.assertIn('new-horizons.combat.reanimate.healed', texts)

    def test_soul_reaper_has_shadow_finisher_data_and_original_art(self):
        row = self.rules['spells']['new-horizons:soulReaper']
        self.assertEqual(row['schools'], ['new-horizons:shadow'])
        self.assertEqual((row['level'], row['costs']), (5, [21, 21, 21, 21]))
        self.assertEqual(row['directDamage'], {'base': 60, 'powerCoefficient': 14})
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['soulReaper']
        self.assertEqual((spell['name'], spell['targetType']), ('Soul Reaper', 'CREATURE'))
        self.assertEqual(spell['targetCondition'], {})
        self.assertTrue(all(spell['flags'][flag]
                            for flag in ('offensive', 'damage', 'negative')))
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 21)
            self.assertEqual(level['battleEffects']['directDamage']['type'], 'damage')
            self.assertFalse(level['targetModifier']['smart'])
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_soul_reaper_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))
        texts = load('config/newHorizonsCombatTexts.json')
        for form in (0, 1, 2):
            self.assertIn(f'new-horizons.combat.soulReaper.execute.{form}', texts)

    def test_doom_has_shadow_malediction_data_and_original_art(self):
        row = self.rules['spells']['new-horizons:doom']
        self.assertEqual(row['schools'], ['new-horizons:shadow'])
        self.assertEqual((row['level'], row['costs']), (5, [25, 25, 25, 25]))
        self.assertNotIn('directDamage', row)
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['doom']
        self.assertEqual((spell['name'], spell['targetType']), ('Doom', 'CREATURE'))
        self.assertEqual(spell['targetCondition'], {})
        self.assertTrue(spell['flags']['offensive'])
        self.assertTrue(spell['flags']['negative'])
        self.assertFalse(spell['flags'].get('damage', False))
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 25)
            self.assertEqual(level['battleEffects']['doom']['type'], 'core:doomEffect')
            self.assertTrue(level['targetModifier']['smart'])
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_doom_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))
        texts = load('config/newHorizonsCombatTexts.json')
        self.assertIn('new-horizons.combat.doom.applied', texts)
        self.assertIn('new-horizons.combat.doom.refreshed', texts)

    def test_sanctuary_is_a_light_single_stack_spell_with_original_art(self):
        row = self.rules['spells']['new-horizons:sanctuary']
        self.assertEqual(row['schools'], ['new-horizons:light'])
        self.assertEqual((row['level'], row['costs']), (1, [5, 5, 5, 5]))
        self.assertNotIn('directDamage', row)
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['sanctuary']
        self.assertEqual((spell['name'], spell['targetType']), ('Sanctuary', 'CREATURE'))
        self.assertTrue(spell['flags']['positive'])
        self.assertFalse(spell['flags'].get('negative', False))
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 5)
            marker = level['battleEffects']['sanctuary']['bonus']['sanctified']
            self.assertEqual((marker['type'], marker['duration']), ('SANCTIFIED', 'ONE_BATTLE'))
            self.assertTrue(level['targetModifier']['smart'])
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_sanctuary_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))

    def test_guardian_spirit_is_a_timed_light_physical_shield(self):
        row = self.rules['spells']['new-horizons:guardianSpirit']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:light'], 2, [8, 8, 8, 8]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['guardianSpirit']
        self.assertEqual((spell['name'], spell['targetType']), ('Guardian Spirit', 'CREATURE'))
        self.assertTrue(spell['flags']['positive'])
        self.assertFalse(spell['flags'].get('damage', False))
        for level in spell['levels'].values():
            self.assertEqual(level['cost'], 8)
            marker = level['battleEffects']['guardianSpirit']['bonus']['guardianSpirit']
            self.assertEqual((marker['type'], marker['duration'], marker['turns']),
                             ('GUARDIAN_SPIRIT', 'N_TURNS', 2))
            self.assertTrue(level['targetModifier']['smart'])
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_guardian_spirit_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))

    def test_heavenly_gale_is_army_wide_fractional_ranged_protection(self):
        row = self.rules['spells']['new-horizons:heavenlyGale']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:light'], 3, [13, 13, 13, 13]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['heavenlyGale']
        self.assertEqual((spell['name'], spell['targetType']), ('Heavenly Gale', 'CREATURE'))
        self.assertTrue(spell['flags']['positive'])
        self.assertFalse(spell['flags'].get('damage', False))
        light_perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:lightMagic']['perks']
        aegis = next(perk for perk in light_perks
                     if perk['id'] == 'new-horizons:lightMagic.aegis')
        self.assertEqual(aegis['effect']['status'], 'active')
        for level in spell['levels'].values():
            self.assertEqual((level['range'], level['cost']), ('X', 13))
            marker = level['battleEffects']['heavenlyGale']['bonus']['rangedProjectileReduction']
            self.assertEqual((marker['type'], marker['val'], marker['duration'], marker['turns']),
                             ('HEAVENLY_GALE', 5000, 'N_TURNS', 2))
            self.assertTrue(level['targetModifier']['smart'])
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_heavenly_gale_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))

    def test_divine_retribution_is_rostered_delayed_light_protection(self):
        row = self.rules['spells']['new-horizons:divineRetribution']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:light'], 4, [16, 16, 16, 16]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['divineRetribution']
        self.assertEqual((spell['name'], spell['targetType']),
                         ('Divine Retribution', 'CREATURE'))
        self.assertTrue(spell['flags']['positive'])
        self.assertFalse(spell['flags'].get('damage', False))
        light_perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:lightMagic']['perks']
        retributionist = next(perk for perk in light_perks
                               if perk['id'] == 'new-horizons:lightMagic.retributionist')
        self.assertEqual(retributionist['effect']['status'], 'active')
        for level in spell['levels'].values():
            self.assertEqual((level['range'], level['cost']), ('0', 16))
            marker = level['battleEffects']['divineRetribution']['bonus']['judgment']
            self.assertEqual((marker['type'], marker['val'], marker['duration'], marker['turns']),
                             ('DIVINE_RETRIBUTION', 25, 'N_TURNS', 2))
            self.assertTrue(level['targetModifier']['smart'])
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_divine_retribution_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))

    def test_entangle_is_ground_enemy_root_with_independent_movement(self):
        row = self.rules['spells']['new-horizons:entangle']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:nature'], 1, [4, 4, 4, 4]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['entangle']
        self.assertEqual((spell['name'], spell['targetType']), ('Entangle', 'CREATURE'))
        self.assertTrue(spell['flags']['negative'])
        self.assertEqual(spell['targetCondition']['noneOf']['bonus.FLYING'], 'absolute')
        base = spell['levels']['base']
        self.assertEqual((base['range'], base['cost']), ('0', 4))
        self.assertTrue(base['targetModifier']['smart'])
        bonuses = base['battleEffects']['entangle']['bonus']
        self.assertEqual(list(bonuses), ['root'])
        self.assertEqual((bonuses['root']['type'], bonuses['root']['duration']),
                         ('BIND_EFFECT', 'N_TURNS'))
        self.assertNotIn('addInfo', bonuses['root'])
        self.assertNotIn('parameters', bonuses['root'])
        perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:natureMagic']['perks']
        self.assertEqual(next(p for p in perks if p['id'].endswith('.rootcaller'))
                         ['effect']['status'], 'active')

    def test_summon_trolls_uses_selected_placement_and_exact_health_effect(self):
        row = self.rules['spells']['new-horizons:summonTrolls']
        self.assertEqual(row['schools'], ['new-horizons:nature'])
        self.assertEqual(row['level'], 2)
        self.assertEqual(row['costs'], [9] * 4)
        self.assertNotIn('directDamage', row)
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['summonTrolls']
        self.assertEqual(spell['targetType'], 'LOCATION')
        self.assertEqual(spell['flags'], {'indifferent': True})
        effect = spell['levels']['base']['battleEffects']['summonTrolls']
        self.assertEqual(effect, {'type': 'core:summonTrolls', 'id': 'core:troll'})
        registration = load('config/scriptsSpells.json')['summonTrolls']
        self.assertEqual(registration['script'], 'spells/summonTrolls')
        for size, key in ((44, 'iconBook'), (32, 'iconScroll'), (30, 'iconEffect')):
            self.assertEqual(spell['graphics'][key], f'NH_spell_summon_trolls_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / spell['graphics'][key]).read_bytes()[16:24]),
                (size, size))
        perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:natureMagic']['perks']
        self.assertEqual(next(p for p in perks if p['id'].endswith('.beastcaller'))
                         ['effect']['status'], 'active')
        self.assertEqual(spell['sounds']['cast'], 'SUMNELM')

    def test_blink_registers_random_single_stack_relocation(self):
        row = self.rules['spells']['new-horizons:blink']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:chaos'], 1, [4] * 4))
        self.assertNotIn('directDamage', row)
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['blink']
        self.assertEqual(spell['targetType'], 'CREATURE')
        self.assertEqual(spell['flags'], {'negative': True})
        base = spell['levels']['base']
        self.assertEqual((base['range'], base['cost']), ('0', 4))
        self.assertEqual(base['targetModifier'], {'smart': False})
        self.assertEqual(base['battleEffects'], {'blink': {'type': 'core:blink'}})
        for rank in ('none', 'basic', 'advanced', 'expert'):
            self.assertEqual(spell['levels'][rank], {})
        self.assertEqual(load('config/scriptsSpells.json')['blink']['script'], 'spells/blink')
        self.assertEqual(spell['sounds']['cast'], 'TELPTOUT')
        for size, key in ((44, 'iconBook'), (32, 'iconScroll'), (30, 'iconEffect')):
            self.assertEqual(spell['graphics'][key], f'NH_spell_blink_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / spell['graphics'][key]).read_bytes()[16:24]),
                (size, size))

    def test_hydras_vitality_registers_single_target_capacity_effect(self):
        row = self.rules['spells']['new-horizons:hydrasVitality']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:nature'], 4, [16] * 4))
        self.assertNotIn('directDamage', row)
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['hydrasVitality']
        # Shared effect values use millionths of one percent so School
        # scaling is not rounded before the final creature-HP calculation.
        self.assertEqual(spell['power'], 150000)
        self.assertEqual(spell['targetType'], 'CREATURE')
        self.assertEqual(spell['flags'], {'positive': True})
        base = spell['levels']['base']
        self.assertEqual((base['range'], base['cost']), ('0', 16))
        self.assertEqual(base['power'], 25000000)
        self.assertEqual(base['battleEffects'],
                         {'hydrasVitality': {'type': 'core:hydrasVitality'}})
        for rank in ('none', 'basic', 'advanced', 'expert'):
            self.assertEqual(spell['levels'][rank], {})
        self.assertEqual(load('config/scriptsSpells.json')['hydrasVitality']['script'],
                         'spells/hydrasVitality')
        for size, key in ((44, 'iconBook'), (32, 'iconScroll'), (30, 'iconEffect')):
            self.assertEqual(spell['graphics'][key], f'NH_spell_hydras_vitality_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / spell['graphics'][key]).read_bytes()[16:24]),
                (size, size))

    def test_verdant_prison_uses_temporary_ring_effect_and_purpose_made_art(self):
        row = self.rules['spells']['new-horizons:verdantPrison']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:nature'], 3, [12] * 4))
        self.assertNotIn('directDamage', row)
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['verdantPrison']
        self.assertEqual(spell['targetType'], 'CREATURE')
        self.assertEqual(spell['flags'], {'negative': True})
        self.assertEqual(spell['levels']['base']['battleEffects']['verdantPrison'],
                         {'type': 'core:verdantPrison', 'id': 'core:dendroidGuard'})
        self.assertEqual(load('config/scriptsSpells.json')['verdantPrison']['script'],
                         'spells/verdantPrison')
        for size, key in ((44, 'iconBook'), (32, 'iconScroll'), (30, 'iconEffect')):
            self.assertEqual(spell['graphics'][key], f'NH_spell_verdant_prison_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / spell['graphics'][key]).read_bytes()[16:24]),
                (size, size))
        perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:natureMagic']['perks']
        self.assertEqual(next(p for p in perks if p['id'].endswith('.verdantWarden'))
                         ['effect']['status'], 'active')

    def test_vengeful_vines_is_oriented_damage_with_fixed_movement_penalty(self):
        row = self.rules['spells']['new-horizons:vengefulVines']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:nature'], 1, [5, 5, 5, 5]))
        # The shared raw-Spell-Power divisor is 10: 11 / 10 = 1.1.
        self.assertEqual(row['directDamage'], {'base': 20, 'powerCoefficient': 11})
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['vengefulVines']
        self.assertEqual(spell['targetType'], 'LOCATION')
        self.assertTrue(spell['flags']['damage'])
        base = spell['levels']['base']
        self.assertEqual((base['cost'], base['range']), (5, '0'))
        self.assertTrue(base['targetModifier']['smart'])
        self.assertEqual(base['battleEffects']['directDamage'], {'type': 'damage'})
        self.assertEqual(base['battleEffects']['slowingVines']['bonus']['speed'],
                         {'type': 'STACKS_MOVEMENT_RANGE', 'val': -2, 'duration': 'N_TURNS', 'turns': 2})
        for size, key in ((44, 'iconBook'), (32, 'iconScroll'), (30, 'iconEffect')):
            filename = spell['graphics'][key]
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]), (size, size))

    def test_shield_of_chaos_is_neutral_single_target_with_separate_defenses(self):
        row = self.rules['spells']['new-horizons:shieldOfChaos']
        self.assertEqual(row['schools'], ['new-horizons:chaos'])
        self.assertEqual(row['level'], 5)
        self.assertEqual(row['costs'], [23] * 4)
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['shieldOfChaos']
        self.assertEqual(spell['targetType'], 'CREATURE')
        self.assertFalse(spell['flags'].get('positive', False))
        self.assertFalse(spell['flags'].get('negative', False))
        for level in spell['levels'].values():
            self.assertEqual(level['range'], '0')
            self.assertFalse(level['targetModifier']['smart'])
            bonuses = level['battleEffects']['shieldOfChaos']['bonus']
            self.assertEqual(bonuses['physicalDamageReduction']['type'],
                             'PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS')
            self.assertEqual(bonuses['magicalDamageReduction']['type'],
                             'SPELL_DAMAGE_REDUCTION_BASIS_POINTS')
            self.assertEqual(bonuses['magicalDamageReduction']['subtype'], 'any')
            self.assertEqual(bonuses['morale']['val'], -10)
            self.assertEqual(bonuses['luck']['val'], -10)
            self.assertTrue(all(b['duration'] == 'N_TURNS' for b in bonuses.values()))

    def test_crusade_is_rostered_full_army_light_empowerment(self):
        row = self.rules['spells']['new-horizons:crusade']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:light'], 5, [24, 24, 24, 24]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['crusade']
        self.assertEqual((spell['name'], spell['targetType']), ('Crusade!', 'CREATURE'))
        self.assertTrue(spell['flags']['positive'])
        perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:lightMagic']['perks']
        self.assertEqual(next(p for p in perks if p['id'].endswith('.crusader'))
                         ['effect']['status'], 'active')
        for level in spell['levels'].values():
            self.assertEqual((level['range'], level['cost']), ('X', 24))
            self.assertTrue(level['targetModifier']['smart'])
            bonuses = level['battleEffects']['crusade']['bonus']
            self.assertEqual(len(bonuses), 5)
            self.assertEqual(bonuses['initiative']['type'], 'STACKS_INITIATIVE_FLAT')
            self.assertEqual((bonuses['magicalDamageReduction']['type'],
                              bonuses['magicalDamageReduction']['val']),
                             ('SPELL_DAMAGE_REDUCTION_BASIS_POINTS', 1200))
            self.assertEqual((bonuses['moraleFloor']['type'], bonuses['moraleFloor']['val']),
                             ('MINIMUM_MORALE', 0))
            self.assertTrue(all(b['duration'] == 'N_TURNS' and b['turns'] == 3
                                for b in bonuses.values()))

    def test_purify_is_rostered_area_light_cleanse(self):
        row = self.rules['spells']['new-horizons:purify']
        self.assertEqual((row['schools'], row['level'], row['costs']),
                         (['new-horizons:light'], 4, [15, 15, 15, 15]))
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['purify']
        self.assertEqual((spell['name'], spell['targetType']), ('Purify', 'LOCATION'))
        self.assertTrue(spell['flags']['positive'])
        self.assertFalse(spell['flags'].get('damage', False))
        light_perks = load('config/newHorizonsPerks.json')['skills']['new-horizons:lightMagic']['perks']
        purifier = next(perk for perk in light_perks
                        if perk['id'] == 'new-horizons:lightMagic.purifier')
        self.assertEqual(purifier['effect']['status'], 'active')
        for level in spell['levels'].values():
            self.assertEqual((level['range'], level['cost']), ('0', 15))
            self.assertEqual(level['battleEffects']['purify']['type'], 'core:purify')
        for role, size in (('iconBook', 44), ('iconScroll', 32),
                           ('iconScenarioBonus', 32), ('iconEffect', 30),
                           ('iconImmune', 30)):
            filename = spell['graphics'][role]
            self.assertEqual(filename, f'NH_spell_purify_{size}.png')
            self.assertEqual(struct.unpack('>II',
                (ROOT / 'Mods/new-horizons/Images' / filename).read_bytes()[16:24]),
                (size, size))

    def test_hand_of_fate_is_a_rostered_chaos_spell_with_recipient_only_spill(self):
        row = self.rules['spells']['new-horizons:handOfFate']
        self.assertEqual(row['schools'], ['new-horizons:chaos'])
        self.assertEqual(row['level'], 3)
        self.assertEqual(row['costs'], [12] * 4)
        self.assertEqual(row['directDamage'], {'base': 70, 'powerCoefficient': 25})
        spell = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['handOfFate']
        self.assertEqual(spell['targetType'], 'CREATURE')
        for rank in ('none', 'basic', 'advanced', 'expert'):
            level = spell['levels'][rank]
            self.assertEqual(level['cost'], 12)
            self.assertEqual(level['range'], '0')
            self.assertEqual(level['battleEffects']['directDamage'],
                             {'type': 'damage', 'handOfFate': True})
            self.assertIn('actual HP loss', level['description'])
            self.assertIn('not rerolled', level['description'])

    def test_holy_wrath_is_a_rostered_single_target_light_damage_spell(self):
        spell_id = 'new-horizons:holyWrath'
        row = self.rules['spells'][spell_id]
        self.assertEqual(row['schools'], ['new-horizons:light'])
        self.assertEqual(row['level'], 3)
        self.assertEqual(row['costs'], [11, 11, 11, 11])
        self.assertEqual(row['directDamage'], {'base': 40, 'powerCoefficient': 2})

        definition = load('Mods/new-horizons/Content/config/spells/newHorizons.json')['holyWrath']
        self.assertEqual(definition['name'], 'Holy Wrath')
        self.assertEqual(definition['type'], 'combat')
        self.assertEqual(definition['school'], {'new-horizons:light': True})
        self.assertEqual(definition['level'], 3)
        self.assertEqual(definition['targetType'], 'CREATURE')
        self.assertEqual(definition['graphics']['iconBook'], 'NH_holyWrath_44.png')
        self.assertEqual(definition['graphics']['iconScroll'], 'NH_holyWrath_44.png')
        self.assertEqual(definition['graphics']['iconEffect'], 'NH_holyWrath_30.png')
        self.assertEqual(definition['graphics']['iconImmune'], 'NH_holyWrath_30.png')
        self.assertTrue((ROOT / 'Mods/new-horizons/Images/NH_holyWrath_44.png').is_file())
        self.assertTrue((ROOT / 'Mods/new-horizons/Images/NH_holyWrath_30.png').is_file())
        self.assertEqual(set(definition['levels']), {'none', 'basic', 'advanced', 'expert'})
        for rank, level in definition['levels'].items():
            with self.subTest(rank=rank):
                self.assertEqual(level['range'], '0')
                self.assertEqual(level['cost'], 11)
                self.assertTrue(level['targetModifier']['smart'])
                self.assertEqual(level['battleEffects']['directDamage'], {'type': 'damage'})
                self.assertEqual(set(level['battleEffects']), {'directDamage'})
                self.assertIn('one enemy stack', level['description'])
                self.assertIn('150%', level['description'])
                self.assertIn('Undead', level['description'])
                self.assertIn('base faction is Inferno', level['description'])

    def test_new_horizons_expert_mass_is_saved_ruleset_scoped_not_global_content(self):
        overlay = load('Mods/new-horizons/Content/config/spells/iceBolt.json')
        spell_overlays = {}
        for path in sorted((ROOT / 'Mods/new-horizons/Content/config/spells').glob('*.json')):
            spell_overlays.update(load(str(path.relative_to(ROOT))))
        core = {}
        for filename in ('adventure', 'offensive', 'other', 'timed'):
            core.update({f'core:{spell}': definition
                         for spell, definition in load(f'config/spells/{filename}.json').items()})
        active_core = {spell_id for spell_id, spell in self.rules['spells'].items()
                       if spell_id.startswith('core:') and spell.get('active', True)}
        expert_wide = {spell_id for spell_id in active_core
                       if core[spell_id]['targetType'] == 'CREATURE'
                       and core[spell_id].get('levels', {}).get('expert', {}).get('range') == 'X'}
        self.assertEqual(len(expert_wide), 22)
        for spell_id in expert_wide:
            with self.subTest(spell=spell_id):
                self.assertEqual(core[spell_id]['levels']['expert']['range'], 'X')
                expert_patch = overlay.get(spell_id, {}).get('levels', {}).get('expert', {})
                self.assertNotIn('range', expert_patch)

        # Berserk uses LOCATION targeting in core data, so it is not caught by
        # the creature-range sweep. Its New Horizons shape is saved-profile
        # mechanics and must not be merged into the shared CSpell content.
        self.assertEqual(core['core:berserk']['targetType'], 'LOCATION')
        self.assertFalse(core['core:berserk']['levels']['base']['targetModifier']['smart'])
        self.assertEqual(core['core:berserk']['levels']['advanced']['range'], '0-1')
        self.assertEqual(core['core:berserk']['levels']['expert']['range'], '0-2')
        self.assertNotIn('core:berserk', overlay)

        # Saved-profile Dispel behavior must not be merged into shared content:
        # legacy v1/v2 worlds retain core targeting and Expert obstacle removal.
        self.assertNotIn('core:dispel', spell_overlays)
        self.assertTrue(core['core:dispel']['levels']['base']['targetModifier']['smart'])
        self.assertFalse(core['core:dispel']['levels']['advanced']['targetModifier']['smart'])
        self.assertFalse(core['core:dispel']['levels']['expert']['targetModifier']['smart'])
        self.assertEqual(core['core:dispel']['levels']['expert']['range'], 'X')
        core_expert_effects = core['core:dispel']['levels']['expert']['battleEffects']
        self.assertTrue(core_expert_effects['dispel']['optional'])
        self.assertIn('removeObstacle', core_expert_effects)

        # The core spell keeps its original rank-dependent target count in
        # shared content; New Horizons v3 resolves its fixed-five behavior from
        # the saved battle profile instead of merging a global spell patch.
        self.assertEqual(core['core:chainLightning']['levels']['base']['battleEffects']['directDamage']['chainLength'], 4)
        self.assertEqual(core['core:chainLightning']['levels']['advanced']['battleEffects']['directDamage']['chainLength'], 5)
        self.assertEqual(core['core:chainLightning']['levels']['expert']['battleEffects']['directDamage']['chainLength'], 5)
        self.assertNotIn('core:chainLightning', overlay)

        self.assertNotIn('core:bless', spell_overlays)
        self.assertNotIn('core:curse', spell_overlays)
        self.assertEqual(
            overlay['core:iceBolt']['levels']['base']['battleEffects']['speedDebuff']
            ['bonus']['stacksMovementRange']['val'], -2)
        for spell_id, effect_name in (
                ('core:bless', 'alwaysMaximumDamage'),
                ('core:curse', 'alwaysMinimumDamage')):
            with self.subTest(spell=spell_id, effect=effect_name):
                # Core data remains unchanged for saved v1/v2 profiles. V3
                # applies its endpoint-only value through saved battle mechanics.
                self.assertEqual(core[spell_id]['levels']['base']['effects'][effect_name]['val'], 0)
                for rank in ('advanced', 'expert'):
                    self.assertEqual(core[spell_id]['levels'][rank]['effects'][effect_name]['val'], 1)

        # Mass remains available as its distinct New Horizons perk, rather than
        # being inherited automatically from Expert mastery.
        perks = load('config/newHorizonsPerks.json')['skills']
        perk_ids = {perk['id'] for skill in perks.values() for perk in skill.get('perks', [])}
        self.assertTrue({
            'new-horizons:lightMagic.litany',
            'new-horizons:shadowMagic.grandMalediction',
            'new-horizons:sorceryMagic.temporalField',
        } <= perk_ids)

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

    def test_physical_reduction_cap_is_optional_bounded_and_v2_or_v3(self):
        self.assertEqual(self.rules['physicalDamageReductionCapPercent'], 80)
        rules = copy.deepcopy(self.rules)
        rules.pop('physicalDamageReductionCapPercent')
        validate_rules(rules)
        for invalid in (-1, 101, 80.5, None, '80', True):
            with self.subTest(invalid=invalid):
                rules['physicalDamageReductionCapPercent'] = invalid
                with self.assertRaises(ValidationError):
                    validate_rules(rules)
        old_rules = legacy_rules(self.rules)
        old_rules['physicalDamageReductionCapPercent'] = 80
        with self.assertRaises(ValidationError):
            validate_rules(old_rules)

    def test_complete_existing_spell_inventory_and_legacy_schema(self):
        self.assertEqual(len(common_spells()), 69)
        self.assertEqual(INACTIVE_CORE_SPELLS, {'core:animateDead'})
        self.assertEqual(set(self.rules['spells']) - common_spells(),
                         NEW_HORIZONS_SPELLS)
        self.assertEqual(set(self.rules['adventureSpells']), set(ADVENTURE_SPELLS))
        self.assertEqual({name: (entry['guildLevel'], entry['cost'])
                          for name, entry in self.rules['adventureSpells'].items()},
                         ADVENTURE_SPELLS)
        self.assertTrue(set(self.rules['spells']).isdisjoint(ADVENTURE_SPELLS))
        validate_rules({})
        validate_rules(legacy_rules(self.rules))
        validate_rules(self.rules)

    def test_canonical_implosion_and_earthquake_school_assignments(self):
        implosion = self.rules['spells']['core:implosion']
        earthquake = self.rules['spells']['core:earthquake']
        self.assertEqual(implosion['schools'], ['new-horizons:sorcery'])
        self.assertEqual(earthquake['schools'], ['new-horizons:nature'])

    def test_specialty_spell_is_not_an_ordinary_acquisition_and_counterspell_is_removed(self):
        master = self.rules['spells']['new-horizons:masterChainLightning']
        self.assertFalse(master['ordinaryAcquisition'])
        self.assertTrue(master.get('active', True))
        self.assertFalse(self.rules['spells']['new-horizons:counterspell']['active'])
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
                              'new-horizons:masterChainLightning',
                              'new-horizons:disintegrate',
                              'new-horizons:holyWrath',
                              'new-horizons:handOfFate',
                              'new-horizons:lifeDrain',
                              'new-horizons:soulReaper',
                              'new-horizons:stormOfDaggers',
                              'new-horizons:vengefulVines',
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
        self.assertEqual(self.rules['spells']['new-horizons:masterChainLightning']['directDamage'],
                         {'base': 130, 'powerCoefficient': 18})
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

    def test_transfigure_matter_reuses_remove_obstacle_icons(self):
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        spell = content['transfigureMatter']
        # Remove Obstacle is index 64; SPELLINT reserves frame zero, as in
        # CSpell::registerIcons. Book, scroll and scenario frames are unshifted.
        self.assertEqual(spell['graphics'], {
            'iconBook': 'SPELLS.def:0:64',
            'iconScroll': 'SPELLSCR.def:0:64',
            'iconEffect': 'SPELLINT.def:0:65',
            'iconImmune': 'SPELLINT.def:0:65',
            'iconScenarioBonus': 'SPELLBON.def:0:64',
        })
        module = load('Mods/new-horizons/mod.json')
        self.assertIn('config/spells/newHorizons.json', module['spells'])
        self.assertIn({'type': 'dir', 'path': '/Content'}, module['filesystem'][''])

    def test_halon_patch_replaces_retired_mysticism_with_canonical_tower_skills(self):
        patch = load('Mods/new-horizons/Content/config/heroes/halon.json')['core:halon']
        self.assertEqual(patch['skills'], [
            {'skill': 'new-horizons:metamagic', 'level': 'basic'},
            {'skill': 'new-horizons:spellcraft', 'level': 'basic'},
        ])
        self.assertEqual(patch['images']['specialtySmall'], 'NH_metamagic_prism_basic_small.png')
        self.assertEqual(patch['images']['specialtyLarge'], 'NH_metamagic_prism_basic_medium.png')
        self.assertEqual(patch['images']['small'], 'HPS041WZ.bmp')
        self.assertEqual(patch['images']['large'], 'HPL041WZ.bmp')
        self.assertIsNone(patch['specialty']['secondary'])
        self.assertIsNone(patch['specialty']['bonuses']['mysticismExtra'])
        self.assertEqual(patch['specialty']['bonuses']['metamagicUses'], {
            'type': 'METAMAGIC_USES_PER_COMBAT',
            'valueType': 'BASE_NUMBER',
            'val': 1,
        })
        self.assertEqual(patch['texts']['specialty'], {
            'name': 'Metamagic Adept',
            'tooltip': 'Metamagic: +1 use per combat',
            'description': 'Halon can use Metamagic one additional time per combat.',
        })
        module = load('Mods/new-horizons/mod.json')
        self.assertEqual(module['heroes'], [
            'config/heroes/biographies.json',
            'config/heroes/fafner.json',
            'config/heroes/halon.json',
            'config/heroes/solmyr.json',
        ])

    def test_fafner_patch_keeps_a_valid_non_faction_skill_with_metamagic(self):
        patch = load('Mods/new-horizons/Content/config/heroes/fafner.json')['core:fafner']
        self.assertEqual(patch['skills'], [
            {'skill': 'new-horizons:learning', 'level': 'basic'},
            {'skill': 'new-horizons:metamagic', 'level': 'basic'},
        ])

    def test_solmyr_uses_master_chain_lightning_loadout_and_excludes_legacy_spell(self):
        patch = load('Mods/new-horizons/Content/config/heroes/solmyr.json')['core:solmyr']
        self.assertEqual(patch['spellbook'], ['new-horizons:masterChainLightning'])
        self.assertEqual(patch['skills'], [
            {'skill': 'new-horizons:metamagic', 'level': 'basic'},
            {'skill': 'new-horizons:havocMagic', 'level': 'basic'},
        ])
        self.assertEqual(patch['startingPerks'], [{
            'skill': 'new-horizons:havocMagic',
            'perk': 'new-horizons:havocMagic.stormcaller',
        }])
        self.assertEqual(patch['excludedSpells'], ['core:chainLightning'])
        self.assertEqual(patch['texts']['specialty'], {
            'name': 'Master Chain Lightning',
            'tooltip': 'Master Chain Lightning',
            'description': "Solmyr's Master Chain Lightning deals the same initial damage as Chain Lightning, but loses less damage with each jump and improves as he gains levels.",
        })
        # The patch deliberately leaves the base Solmyr specialty art in place.
        self.assertNotIn('images', patch)
        self.assertIsNone(patch['specialty']['spellScalingPercentage'])

        regular = self.rules['spells']['core:chainLightning']
        master = self.rules['spells']['new-horizons:masterChainLightning']
        self.assertEqual(master['costs'], regular['costs'])
        self.assertEqual(master['directDamage'], regular['directDamage'])

        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        master_levels = content['masterChainLightning']['levels']
        self.assertEqual(master_levels['none']['battleEffects']['directDamage']['chainFactor'], 0.75)
        self.assertEqual(master_levels['none']['battleEffects']['directDamage']['chainFactorPerHeroLevel'], 0.01)
        self.assertEqual(master_levels['none']['battleEffects']['directDamage']['chainFactorMaximum'], 0.9)
        self.assertEqual(master_levels['none']['battleEffects']['directDamage']['chainLength'], 4)
        self.assertEqual(master_levels['advanced']['battleEffects']['directDamage']['chainLength'], 5)
        self.assertGreater(master_levels['none']['battleEffects']['directDamage']['chainFactor'],
                           0.5)
        self.assertEqual(0.75 + 0.01 * 1, 0.76)
        # The saved direct-damage formula is identical at every hero level;
        # only target indices after the first use the level-scaled retention.
        base, coefficient, power, divisor = 130, 18, 40, 10
        first_target = base + coefficient * power // divisor
        self.assertEqual(first_target, 130 + 18 * power // divisor)
        retention = lambda level: min(0.9, 0.75 + 0.01 * level)
        self.assertGreater(retention(1), 0.5)
        self.assertGreater(retention(10), retention(1))
        self.assertEqual(first_target, base + coefficient * power // divisor)

    def test_tower_unique_buildings_use_new_horizons_identity_and_effects(self):
        patch = load('Mods/new-horizons/Content/config/factions/uniqueBuildings.json')['core:tower']['town']['buildings']
        self.assertEqual(patch['special2']['name'], 'Astronomy Tower')
        self.assertEqual(patch['special2']['bonuses'], [])
        self.assertEqual(patch['special3']['name'], 'Library')
        self.assertEqual(patch['special3']['type'], 'library')
        self.assertEqual(patch['special3']['requires'],
            ['allOf', ['dwellingLvl4'], ['mageGuild4']])
        self.assertEqual(patch['special3']['cost'], {
            'gold': 15000, 'wood': 10, 'ore': 10, 'mercury': 5,
            'sulfur': 5, 'crystal': 5, 'gems': 5,
        })
        # The +1 is applied by the authoritative town-growth path so it can
        # distinguish Mage / Arch Mage from Tower's other level-4 dwelling.
        self.assertEqual(patch['special3']['bonuses'], [])
        self.assertEqual(patch['special4']['name'], 'Arcane Reservoir')
        self.assertNotIn('type', patch['special4'])
        self.assertTrue(patch['special4']['manualHeroVisit'])
        self.assertEqual(patch['special4']['configuration']['resetParameters'],
            {'weeks': 1, 'visitors': True})
        self.assertEqual(patch['special4']['configuration']['visitMode'], 'once')

    def test_inferno_brimstone_stormclouds_produce_sulfur_and_remove_legacy_bonus(self):
        patch = load('Mods/new-horizons/Content/config/factions/uniqueBuildings.json')['core:inferno']['town']['buildings']['special2']
        self.assertEqual(patch['name'], 'Brimstone Stormclouds')
        self.assertEqual(patch['produce'], {'sulfur': 1})
        self.assertEqual(patch['bonuses'], [])

    def test_necromancy_amplifier_is_a_seven_day_hero_reward_not_a_kingdom_aura(self):
        patch = load('Mods/new-horizons/Content/config/factions/uniqueBuildings.json')['core:necropolis']['town']['buildings']['special2']
        self.assertEqual(patch['bonuses'], [])
        self.assertEqual(patch['configuration']['visitMode'], 'unlimited')
        reward, = patch['configuration']['rewards']
        self.assertEqual(reward['limiter']['heroClasses'],
                         ['core:deathknight', 'core:necromancer'])
        bonus, = reward['bonuses']
        self.assertEqual(bonus, {
            'type': 'UNDEAD_RAISE_PERCENTAGE', 'val': 10,
            'duration': 'N_DAYS', 'turns': 7,
            'stacking': 'newHorizonsNecromancyAmplifier',
        })
        self.assertNotIn('playerBonuses', reward)
        self.assertNotIn('propagator', bonus)

    def test_universal_mage_guild_overlay_reaches_level_five(self):
        overlay = load('Mods/new-horizons/Content/config/factions/universalMageGuilds.json')
        module = load('Mods/new-horizons/mod.json')
        self.assertIn('config/factions/universalMageGuilds.json', module['factions'])

        building_validator = Draft4Validator(load('config/schemas/townBuilding.json'))
        structure_validator = Draft4Validator(load('config/schemas/townStructure.json'))
        expected_factions = {
            'castle': ('mageGuild5',),
            'rampart': (),
            'tower': (),
            'inferno': (),
            'necropolis': (),
            'dungeon': (),
            'stronghold': ('mageGuild4', 'mageGuild5'),
            'fortress': ('mageGuild4', 'mageGuild5'),
            'conflux': (),
        }
        expected_original_maximum = {
            'castle': 4,
            'rampart': 5,
            'tower': 5,
            'inferno': 5,
            'necropolis': 5,
            'dungeon': 5,
            'stronghold': 3,
            'fortress': 3,
            'conflux': 5,
        }
        expected_costs = {
            'mageGuild4': {
                'gold': 5000, 'mercury': 5, 'sulfur': 5,
                'crystal': 5, 'gems': 5,
            },
            'mageGuild5': {
                'gold': 10000, 'mercury': 10, 'sulfur': 10,
                'crystal': 10, 'gems': 10,
            },
        }
        expected_predecessor = {
            'mageGuild4': 'mageGuild3',
            'mageGuild5': 'mageGuild4',
        }
        for faction, missing_levels in expected_factions.items():
            with self.subTest(faction=faction):
                patch = overlay['core:' + faction]['town']
                if faction in ('castle', 'stronghold', 'fortress'):
                    self.assertEqual(patch['mageGuild'], 5)
                    self.assertEqual(patch['hallSlots'][1][1], [
                        'mageGuild1', 'mageGuild2', 'mageGuild3',
                        'mageGuild4', 'mageGuild5'])
                    self.assertEqual(len(patch['guildSpellPositions']), 5)
                    self.assertEqual(len(patch['guildSpellPositions'][-1]), 2)

                for level, expected_cost in expected_costs.items():
                    building = patch['buildings'][level]
                    self.assertIsNotNone(building)
                    # #override replaces the legacy wood/ore and faction
                    # resource costs instead of merely adding to them.
                    self.assertEqual(building.get('cost#override'), expected_cost)
                    normalized = copy.deepcopy(building)
                    normalized['cost'] = normalized.pop('cost#override')
                    building_validator.validate(normalized)
                    if level in missing_levels:
                        self.assertEqual(building['upgrades'], expected_predecessor[level])
                    else:
                        # Existing factions retain their core/library
                        # prerequisites and upgrade links untouched.
                        self.assertNotIn('upgrades', building)
                        self.assertNotIn('requires', building)

                    if level in missing_levels:
                        structure = patch['structures'][level]
                        self.assertIsNotNone(structure)
                        structure_validator.validate(structure)
                        self.assertTrue(structure['animation'])

                # Applying this partial patch to the core record leaves all
                # original fields in place while adding the missing guild
                # records and replacing town metadata as intended.
                base = load('config/factions/' + faction + '.json')[faction]
                merged = copy.deepcopy(base)
                for key, value in patch.items():
                    if isinstance(value, dict):
                        merged['town'][key].update(copy.deepcopy(value))
                    else:
                        merged['town'][key] = copy.deepcopy(value)
                self.assertEqual(merged['town']['mageGuild'], 5)
                for level in range(1, 6):
                    self.assertIn('mageGuild' + str(level),
                                  merged['town']['hallSlots'][1][1])
                    self.assertIsNotNone(merged['town']['buildings'].get(
                        'mageGuild' + str(level)))

                # The original mode remains untouched: the core still carries
                # its historical maximums and null placeholders.
                self.assertEqual(base['town']['mageGuild'],
                                 expected_original_maximum[faction])
                if missing_levels:
                    self.assertIsNone(base['town']['buildings'].get('mageGuild5'))
                else:
                    self.assertIsInstance(base['town']['buildings'].get('mageGuild5'), dict)

    def test_dungeon_astral_nexus_replenishes_mana_to_normal_maximum(self):
        patch = load('Mods/new-horizons/Content/config/factions/uniqueBuildings.json')['core:dungeon']['town']['buildings']['special2']
        self.assertEqual(patch['name'], 'Astral Nexus')
        self.assertIn('replenishes', patch['description'])
        configuration = patch['configuration']
        self.assertEqual(configuration['visitMode'], 'unlimited')
        self.assertNotIn('resetParameters', configuration)
        self.assertEqual(len(configuration['rewards']), 1)
        reward = configuration['rewards'][0]
        self.assertEqual(reward['manaPercentage'], 100)
        self.assertIn('spell points', reward['message'].lower())
        self.assertNotIn('primary', reward)

    def test_dungeon_academy_grants_a_quarter_of_remaining_level_experience_once_per_hero(self):
        patches = load('Mods/new-horizons/Content/config/factions/uniqueBuildings.json')
        patch = patches['core:dungeon']['town']['buildings']['special4']
        self.assertEqual(patch['name'], 'Battle Scholar Academy')
        configuration = patch['configuration']
        self.assertEqual(configuration['visitMode'], 'hero')
        self.assertNotIn('resetParameters', configuration)
        self.assertEqual(len(configuration['rewards']), 1)
        reward = configuration['rewards'][0]
        self.assertEqual(reward['heroExperienceNextLevelPercent'], 25)
        self.assertEqual(reward['heroExperience'], 0)
        self.assertNotIn('heroLevel', reward)
        self.assertNotIn('special4', patches['core:castle']['town']['buildings'])
        legacy = load('config/factions/dungeon.json')['dungeon']['town']['buildings']['special4']['configuration']['rewards'][0]
        self.assertEqual(legacy['heroExperience'], 1000)
        self.assertNotIn('heroExperienceNextLevelPercent', legacy)

    def test_legacy_secondary_specialties_are_neutralized_without_erasing_other_bonuses(self):
        patches = load('Mods/new-horizons/Content/config/heroes/halon.json')
        self.assertTrue(set(NH_FACTION_SPECIALTY_PRESENTATIONS) <= set(patches))
        for hero, (name, icon_stem) in NH_FACTION_SPECIALTY_PRESENTATIONS.items():
            with self.subTest(hero=hero):
                patch = patches[hero]
                self.assertIsNone(patch['specialty']['secondary'])
                self.assertEqual(patch['texts']['specialty']['name'], name)
                self.assertEqual(patch['texts']['specialty']['tooltip'],
                                 'Faction skill: ' + name)
                self.assertNotIn('Eagle Eye', patch['texts']['specialty']['description'])
                self.assertNotIn('First Aid', patch['texts']['specialty']['description'])
                self.assertNotIn('Intelligence', patch['texts']['specialty']['description'])
                self.assertNotIn('Mysticism', patch['texts']['specialty']['description'])
                self.assertNotIn('Resistance', patch['texts']['specialty']['description'])
                self.assertNotIn('Sorcery', patch['texts']['specialty']['description'])
                if icon_stem == 'necromancy':
                    expected_small, expected_large = 'SECSK32:0:39', 'SECSKILL:0:39'
                elif icon_stem == 'metamagic':
                    expected_small = 'NH_metamagic_prism_basic_small.png'
                    expected_large = 'NH_metamagic_prism_basic_medium.png'
                    self.assertTrue((ROOT / 'Mods/new-horizons/Images' /
                                     expected_small).is_file())
                    self.assertTrue((ROOT / 'Mods/new-horizons/Images' /
                                     expected_large).is_file())
                else:
                    expected_small = 'NH_' + icon_stem + '_basic_small.png'
                    expected_large = 'NH_' + icon_stem + '_basic_medium.png'
                    self.assertTrue((ROOT / 'Mods/new-horizons/Images' /
                                     expected_small).is_file())
                    self.assertTrue((ROOT / 'Mods/new-horizons/Images' /
                                     expected_large).is_file())
                self.assertEqual(patch['images']['specialtySmall'], expected_small)
                self.assertEqual(patch['images']['specialtyLarge'], expected_large)
                if expected_large.endswith('.png'):
                    self.assertEqual(png_size(ROOT / 'Mods/new-horizons/Images' /
                                              expected_large), (44, 44))
        for hero in ('jaegar', 'rosic', 'axsis'):
            with self.subTest(mysticism_bonus=hero):
                self.assertIsNone(
                    patches['core:' + hero]['specialty']['bonuses']['mysticismExtra'])
        # The explicit Metamagic specialty remains intact; only its retired
        # Mysticism hook is cleared.
        self.assertEqual(patches['core:halon']['specialty']['bonuses']['metamagicUses']['val'], 1)

    def test_phantom_army_is_a_common_active_sorcery_spell_and_replaces_clone(self):
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        phantom = content['phantomArmy']
        self.assertFalse(phantom['flags'].get('special', False))
        self.assertEqual(phantom['school'], {'new-horizons:sorcery': True})
        self.assertEqual(phantom['level'], 4)
        for rank in ('none', 'basic', 'advanced', 'expert'):
            self.assertEqual(phantom['levels'][rank]['cost'], 15)
        self.assertEqual(self.rules['spells']['new-horizons:phantomArmy'], {
            'schools': ['new-horizons:sorcery'],
            'level': 4,
            'costs': [15, 15, 15, 15],
        })
        self.assertFalse(self.rules['spells']['core:clone']['active'])

    def test_focus_magic_is_a_common_sorcery_spell_in_new_world_roster(self):
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        spell = content['focusMagic']
        Draft4Validator(load('config/schemas/spell.json')).validate(spell)
        self.assertEqual(spell['type'], 'combat')
        self.assertEqual(spell['school'], {'new-horizons:sorcery': True})
        self.assertEqual(spell['level'], 3)
        self.assertEqual(spell['targetType'], 'CREATURE')
        self.assertTrue(spell['flags']['positive'])
        self.assertFalse(spell['flags'].get('negative', False))
        self.assertFalse(spell['flags']['special'])
        self.assertEqual(spell['defaultGainChance'], 0)
        self.assertEqual(spell['gainChance'], {})
        self.assertEqual(set(spell['levels']), {'none', 'basic', 'advanced', 'expert'})

        description = (
            "Enchant one friendly ranged-capable stack for 3 rounds. After each ranged creature "
            "attack that deals damage, the damaged enemy stack gains one Arcane Breach mark; the "
            "hit that applies a mark does not benefit from it. A stack can hold at most 3 marks, "
            "and applying a mark refreshes all existing marks to 2 rounds. Focus Magic captures "
            "its caster's Spell Power-derived penetration when cast, and each mark copies that "
            "value: min(20%, 10% + 0.05% x Spell Power x Sorcery rank coefficient). Under saved "
            "v3 rules, the coefficient is 100% with no rank, 115% at Basic, 130% at Advanced, and "
            "145% at Expert; it scales only the Spell Power term, leaving the 10% base unchanged. "
            "Saved v1/v2 profiles use 100% at every Sorcery rank. Subsequent friendly ranged "
            "creature attacks ignore that much Creature Defense; Creature Defense is not reduced, "
            "and melee attacks gain no benefit."
        )
        for rank in ('none', 'basic', 'advanced', 'expert'):
            with self.subTest(rank=rank):
                current = spell['levels'][rank]
                self.assertEqual(current['cost'], 11)
                self.assertEqual(current['power'], 0)
                self.assertEqual(current['description'], description)
                self.assertEqual(current['battleEffects'], {
                    'focusMagic': {'type': 'core:focusMagicEnchantment'},
                })
                self.assertEqual(current['targetModifier'], {'smart': True})

        self.assertEqual(self.rules['spells']['new-horizons:focusMagic'], {
            'schools': ['new-horizons:sorcery'],
            'level': 3,
            'costs': [11, 11, 11, 11],
        })
        self.assertNotIn('new-horizons:focusMagic', self.rules['adventureSpells'])
        scripts = load('config/scriptsSpells.json')
        self.assertEqual(scripts['focusMagicEnchantment']['implements'], 'spellEffect')
        self.assertEqual(scripts['focusMagicEnchantment']['script'], 'spells/focusMagic')
        combat_scripts = load('config/scriptsCombat.json')
        self.assertEqual(combat_scripts['focusMagic']['implements'], 'combatEvent')
        self.assertEqual(combat_scripts['focusMagic']['script'], 'combat/focusMagic')
        self.assertTrue((ROOT / 'scripts/spells/focusMagic.lua').is_file())

    def test_focus_magic_mark_logs_are_localized_and_authoritative_only(self):
        texts = load('config/newHorizonsCombatTexts.json')
        applied_key = 'new-horizons.combat.arcaneBreach.applied'
        refreshed_key = 'new-horizons.combat.arcaneBreach.refreshed'
        applied = texts[applied_key]
        refreshed = texts[refreshed_key]

        self.assertTrue({
            applied_key,
            refreshed_key,
            'new-horizons.combat.arcaneBreach.side.attacker',
            'new-horizons.combat.arcaneBreach.side.defender',
        }.issubset(texts))
        self.assertEqual(texts['new-horizons.combat.arcaneBreach.side.attacker'], 'attacking')
        self.assertEqual(texts['new-horizons.combat.arcaneBreach.side.defender'], 'defending')
        for message in (applied, refreshed):
            with self.subTest(message=message):
                self.assertEqual(message.count('%s'), 2)
                self.assertEqual(message.count('%d'), 5)
                self.assertIn('%d.%d%d%', message)
                self.assertIn('for %d rounds', message)
                self.assertIn('ranged attacks', message)
                self.assertIn('Creature Defense', message)
                self.assertIn('total marks', message)
                self.assertIn("side's marks let its ranged attacks ignore", message)
                self.assertNotIn('basis point', message.lower())
        self.assertIn('now at %d of 3 total marks', applied)
        self.assertIn('refreshed at %d of 3 total marks', refreshed)

        script = (ROOT / 'scripts/combat/focusMagic.lua').read_text(encoding='utf-8')
        self.assertIn('if server:describeChanges() then', script)
        self.assertIn('server:appendLog(battle, {', script)
        self.assertIn('target:getCreature():getNameTextID(target:getCount())', script)
        self.assertIn(applied_key, script)
        self.assertIn(refreshed_key, script)
        self.assertIn('currentMark:getParametersAsJson()', script)
        self.assertIn('parameters.beneficiarySide == beneficiarySide', script)
        self.assertIn('combinedPenetrationBasisPoints + perMarkBasisPoints', script)
        self.assertIn('math.min(currentMark:getVal(), MAX_MARK_PENETRATION_BASIS_POINTS)', script)
        self.assertIn('MAX_MARKS * MAX_MARK_PENETRATION_BASIS_POINTS', script)
        self.assertIn('if validMarks >= MAX_MARKS then break end', script)
        self.assertIn('currentMarks:size(), combinedPenetrationBasisPoints', script)
        self.assertIn('wholePercent = math.floor(combinedPenetrationBasisPoints / 100)', script)
        self.assertIn('tenthsPercent = math.floor(combinedPenetrationBasisPoints / 10) % 10', script)
        self.assertIn('hundredthsPercent = combinedPenetrationBasisPoints % 10', script)

    def test_spell_lock_logs_are_localized_and_describe_lock_effects(self):
        texts = load('config/newHorizonsCombatTexts.json')
        preserve_beneficial = 'new-horizons.combat.spellLock.preserveBeneficial'
        preserve_hostile = 'new-horizons.combat.spellLock.preserveHostile'
        self.assertIn(preserve_beneficial, texts)
        self.assertIn(preserve_hostile, texts)

        for key in (preserve_beneficial, preserve_hostile):
            message = texts[key]
            with self.subTest(key=key):
                self.assertEqual(message.count('%s'), 1)
                self.assertEqual(message.count('%d'), 1)
                self.assertIn('existing magic is frozen', message)
                self.assertIn('no further magic can affect it', message)
                self.assertIn('Orders still can', message)

        beneficial = texts[preserve_beneficial]
        hostile = texts[preserve_hostile]
        self.assertIn('removing hostile magic and preserving its beneficial magic', beneficial)
        self.assertIn('removing beneficial magic and preserving its hostile magic', hostile)

        script = (ROOT / 'scripts/spells/spellLock.lua').read_text(encoding='utf-8')
        self.assertIn('server:appendLog(battle, {', script)
        self.assertIn(preserve_beneficial, script)
        self.assertIn(preserve_hostile, script)

    def test_spell_lock_is_an_active_sorcery_spell(self):
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        spell = content['spellLock']
        Draft4Validator(load('config/schemas/spell.json')).validate(spell)
        self.assertEqual(spell['school'], {'new-horizons:sorcery': True})
        self.assertEqual(spell['level'], 5)
        self.assertFalse(spell['flags'].get('special', False))
        self.assertTrue(spell['flags']['indifferent'])
        self.assertEqual(spell['defaultGainChance'], 0)
        self.assertEqual(spell['gainChance'], {})
        self.assertEqual(self.rules['spells']['new-horizons:spellLock'], {
            'schools': ['new-horizons:sorcery'],
            'active': True,
            'level': 5,
            'costs': [22, 22, 22, 22],
        })
        self.assertEqual(set(spell['levels']), {'none', 'basic', 'advanced', 'expert'})
        for rank in ('none', 'basic', 'advanced', 'expert'):
            current = spell['levels'][rank]
            self.assertEqual(current['cost'], 22)
            self.assertEqual(current['battleEffects']['spellLock']['type'],
                             'core:spellLock')

    def test_arcane_breach_is_only_a_negative_internal_status_identity(self):
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        spell = content['arcaneBreach']
        Draft4Validator(load('config/schemas/spell.json')).validate(spell)
        self.assertEqual(spell['type'], 'combat')
        self.assertTrue(spell['flags']['negative'])
        self.assertTrue(spell['flags']['special'])
        self.assertFalse(spell['flags']['persistent'])
        self.assertEqual(spell['defaultGainChance'], 0)
        self.assertEqual(spell['gainChance'], {})
        self.assertNotIn('new-horizons:arcaneBreach', self.rules['spells'])
        self.assertNotIn('new-horizons:arcaneBreach', self.rules['adventureSpells'])
        for rank in ('none', 'basic', 'advanced', 'expert'):
            with self.subTest(rank=rank):
                level = spell['levels'][rank]
                self.assertEqual(level['cost'], 0)
                self.assertNotIn('battleEffects', level)

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
        settings['artifacts'] = load('config/newHorizonsArtifacts.json')
        settings['creatures'] = {
            'newHorizonsCategories': load('config/newHorizonsCreatureCategories.json')}
        settings['magic'] = {'newHorizons': self.rules}
        settings['heroes'] = {'newHorizons': load('config/newHorizonsHeroes.json'),
                              'newHorizonsCapabilities': load('config/newHorizonsCapabilities.json'),
                              'newHorizonsMasteries': load('config/newHorizonsMasteries.json'),
                              'newHorizonsPerks': load('config/newHorizonsPerks.json')}
        self.assertEqual(module['settings'], settings)
        self.assertEqual(module['version'], load('config/newHorizonsVersion.json')['version'])
        self.assertEqual(module['heroes'], [
            'config/heroes/biographies.json',
            'config/heroes/fafner.json',
            'config/heroes/halon.json',
            'config/heroes/solmyr.json',
        ])
        self.assertIn('Magic Arrow', module['description'])
        self.assertIn('Overcharge', module['description'])
        self.assertEqual(module['spellSchools'], load('config/newHorizonsSchools.json'))
        self.assertEqual(module['skills'], load('config/newHorizonsSkills.json'))
        self.assertEqual(module['filesystem']['SPRITES/'], [{'type': 'dir', 'path': '/Images'}])
        translations = load('config/newHorizonsMasteryTexts.json')
        translations.update(load('config/newHorizonsCreatureCategoryTexts.json'))
        translations.update(load('config/newHorizonsFortTexts.json'))
        translations.update(load('config/newHorizonsMusterTexts.json'))
        translations.update(load('config/newHorizonsHeroClassTexts.json'))
        translations.update(load('config/newHorizonsCombatTexts.json'))
        translations.update(load('config/newHorizonsAdventureSpellTexts.json'))
        self.assertEqual(module['translations'], translations)
        self.assertEqual(module['bonuses'], load('config/newHorizonsConvenienceBonuses.json'))
        self.assertEqual(module['filesystem'][''], [{'type': 'dir', 'path': '/Content'}])
        self.assertFalse(module['keepDisabled'])
        magic_schema = load('config/schemas/gameSettings.json')['properties']['magic']['properties']['newHorizons']
        self.assertEqual(magic_schema['anyOf'], [
            {'$ref': 'newHorizonsMagic.json'},
            {'$ref': 'newHorizonsMagicV2.json'},
            {'$ref': 'newHorizonsMagicV3.json'},
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

    def test_settled_faction_pairs_and_no_fictional_new_spells(self):
        self.assertFalse(self.rules['factions']['core:necropolis']['provisional'])
        self.assertFalse(self.rules['factions']['core:fortress']['provisional'])
        self.assertEqual(set(self.rules['spells']['core:blind']['schools']),
                         {'new-horizons:shadow', 'new-horizons:chaos'})
        self.assertNotIn('core:titanBolt', self.rules['spells'])
        self.assertNotIn('core:poison', self.rules['spells'])
        self.assertIn('new-horizons:timeStop', self.rules['spells'])

    def test_fixed_mage_guild_preferences_match_the_canonical_faction_matrix(self):
        expected = {
            'core:castle': ('new-horizons:light', 'new-horizons:sorcery'),
            'core:rampart': ('new-horizons:nature', 'new-horizons:light'),
            'core:tower': ('new-horizons:sorcery', 'new-horizons:havoc'),
            'core:inferno': ('new-horizons:chaos', 'new-horizons:havoc'),
            'core:necropolis': ('new-horizons:shadow', 'new-horizons:sorcery'),
            'core:dungeon': ('new-horizons:havoc', 'new-horizons:shadow'),
            'core:stronghold': ('new-horizons:chaos', 'new-horizons:nature'),
            'core:fortress': ('new-horizons:nature', 'new-horizons:shadow'),
            'core:conflux': ('new-horizons:havoc', 'new-horizons:nature'),
        }
        actual = {
            faction: (entry['preferredA'], entry['preferredB'])
            for faction, entry in self.rules['factions'].items()
        }
        self.assertEqual(actual, expected)

    def test_fixed_mage_guilds_disable_spell_research_in_client_and_server(self):
        client = (ROOT / 'client/windows/CCastleInterface.cpp').read_text(
            encoding='utf-8')
        self.assertIn(
            'if(!newHorizonsMagic::mageGuildGenerationActive(GAME->interface()->cb->getMagicRules())\n'
            '\t\t&& GAME->interface()->cb->getSettings().getBoolean(EGameSettings::TOWNS_SPELL_RESEARCH)',
            client)
        server = (ROOT / 'server/CGameHandler.cpp').read_text(encoding='utf-8')
        self.assertIn(
            'if(newHorizonsMagic::mageGuildGenerationActive(gameInfo().getMagicRules())\n'
            '\t\t&& complain("Spell research is unavailable with fixed New Horizons Mage Guilds!"))',
            server)

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
            lambda r: r['mageGuildGeneration']['nonPreferredSlots'].__setitem__(0, 4),
            lambda r: r['factions']['core:castle'].__setitem__('preferredB', 'new-horizons:light'),
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
