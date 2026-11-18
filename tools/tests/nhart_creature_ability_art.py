"""Exact native ability artwork and current consumer bindings for the selected wave."""
import hashlib
from io import BytesIO
import json
from pathlib import Path
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RECORD = 'assets/new-horizons/creature-ability-art-selections.json'
RECORD_SHA256 = 'c714cead8e74d55ade77e679f1fff239fd8ea8a25dcd86e36142276ad3061517'
WAVE_SHA256 = '147198c95cca6d8bebc0c979c502219ac8fbba7d6475b4bd212b687a7fd14dc4'


def ability_wave(waves):
    matches = [wave for wave in waves if 'creatureAbilityArtwork' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate creature ability artwork wave')
    wave = matches[0]
    if hashlib.sha256(json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()).hexdigest() != WAVE_SHA256:
        raise ValueError('Selected creature ability artwork wave changed')
    return wave


def validate_ability_art(wave, entries, reader):
    ability_wave([wave])
    payload = (ROOT / RECORD).read_bytes()
    if hashlib.sha256(payload).hexdigest() != RECORD_SHA256:
        raise ValueError('Creature ability artwork provenance changed')
    selected = json.loads(payload)
    exports = selected['exports']
    resources = {row['resource'] for row in exports}
    if (len(exports) != 33 or len(resources) != 33 or resources != set(wave['resources'])
            or wave['creatureAbilityArtwork'] != [{'resources': wave['resources'],
                                                  'selectionRecordSha256': RECORD_SHA256}]
            or {row['resource'] for row in selected['bindings']} != resources):
        raise ValueError('Incomplete or unused creature ability artwork')
    lookup = {row['resource']: row for row in entries}
    paintings = set()
    for row in exports:
        name = row['resource']
        data = reader(name)
        if (hashlib.sha256(data).hexdigest() != row['sha256']
                or lookup[name]['sha256'] != row['sha256'] or not row['masterSha256']):
            raise ValueError('Creature ability artwork bytes or origin changed')
        image = Image.open(BytesIO(data))
        image.load()
        if (image.mode, image.size, image.info) != ('RGBA', (44, 44), {}):
            raise ValueError('Creature ability artwork canvas or metadata changed')
        alpha = image.getchannel('A')
        bounds = alpha.getbbox()
        if (alpha.getextrema() != (0, 255) or list(bounds) != row['alphaBounds']
                or bounds[0] < 2 or bounds[1] < 2 or bounds[2] > 42 or bounds[3] > 42):
            raise ValueError('Creature ability artwork transparency or inset changed')
        paintings.add(hashlib.sha256(image.tobytes()).hexdigest())
    if len(paintings) != 33:
        raise ValueError('Distinct ability paintings were replaced by duplicate art')
    sys.path.insert(0, str(ROOT / 'tools'))
    from import_new_horizons_spell_art import parse_jsonc
    bonuses = parse_jsonc((ROOT / 'config/bonuses.json').read_text())
    for row in selected['bindings']:
        if row['source'] == 'config/bonuses.json' and 'pointer' in row:
            value = bonuses
            for key in row['pointer'].strip('/').split('/'):
                value = value[key]
            if 'SPRITES/' + value != row['resource']:
                raise ValueError('Creature ability configuration binding changed')
        elif row['source'] == 'client/windows/NewHorizonsCreatureAbilityHelp.h' and 'literal' in row:
            if ('"' + row['literal'] + '"' not in (ROOT / row['source']).read_text()
                    or 'SPRITES/' + row['literal'] != row['resource']):
                raise ValueError('Freezing Touch presentation binding changed')
        else:
            raise ValueError('Unrecognized creature ability art consumer')

    validate_named_spell_subtypes(bonuses, selected['namedSpellSubtypeBindings'])


def validate_named_spell_subtypes(bonuses, recorded):
    named = []
    for bonus, definition in bonuses.items():
        if not isinstance(definition, dict):
            continue
        for subtype, icon in definition.get('graphics', {}).get('subtypeIcons', {}).items():
            if subtype.startswith('spell.'):
                named.append({'source': 'config/bonuses.json',
                              'pointer': '/' + bonus + '/graphics/subtypeIcons/' + subtype,
                              'icon': icon})
    if sorted(named, key=lambda row: row['pointer']) != recorded:
        raise ValueError('Named spell ability subtype artwork paths changed')
    expected = {
        '/SPELLCASTER/graphics/subtypeIcons/spell.summonDemons': 'NH_ability_raise_demons.png',
        '/SPELL_AFTER_ATTACK/graphics/subtypeIcons/spell.acidBreath': 'NH_ability_acid_breath.png',
        '/SPELL_AFTER_ATTACK/graphics/subtypeIcons/spell.acidBreathDamage': 'NH_ability_acid_breath.png',
        '/SPELL_AFTER_ATTACK/graphics/subtypeIcons/spell.dispelHelpful': 'SPELLS.def:0:35',
        '/SPELL_LIKE_ATTACK/graphics/subtypeIcons/spell.fireballAbility': 'SPELLS.def:0:21',
    }
    if {row['pointer']: row['icon'] for row in named} != expected:
        raise ValueError('Original typed spell icons or new explicit ability identities changed')
