"""Finite owner-approved Warcasting artwork (new skill new-horizons:warcast, 2026-11-23/24).

Twelve v5 rank images (three ranks in the four secondary-skill roles) and ten perk
icons (native 44/32, four derived 44px button states and their descriptor).
The pack wave and its pixels are validated on their own; the config and perk-icon
bindings are a separate check (validate_bindings) owned by the skill's consumers.
"""
import hashlib
from io import BytesIO
import json
from pathlib import Path
from PIL import Image, ImageEnhance

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/warcasting-art-selections.json'
SELECTION_SHA256 = 'c74099645023b19131f49fc840d2539de638cd164cfd884890aeb6dd7e084e65'
WAVE_SHA256 = '1362e002c4eb18a5421e0ea9c055fb06c6e7801b930042fc67c2e135fc50ca04'
SKILL = 'new-horizons:warcast'
RANKS = ('basic', 'advanced', 'expert')
SIZES = {'small': (32, 32), 'medium': (44, 44), 'large': (82, 93), 'scenarioBonus': (58, 64)}
STATES = ('normal', 'pressed', 'disabled', 'highlighted')
PERKS = {SKILL + '.fieldEnchanter': ('Field Enchanter', 'NH_perk_warcasting2_field_enchanter'),
         SKILL + '.kindledEdge': ('Kindled Edge', 'NH_perk_warcasting2_kindled_edge'),
         SKILL + '.commandersMeditation': ("Commander's Meditation", 'NH_perk_warcasting2_commanders_meditation'),
         SKILL + '.layeredEnchantment': ('Layered Enchantment', 'NH_perk_warcasting2_layered_enchantment'),
         SKILL + '.sustainingCommand': ('Sustaining Command', 'NH_perk_warcasting2_sustaining_command'),
         SKILL + '.opening': ('Opening', 'NH_perk_warcasting2_opening'),
         SKILL + '.wardedRanks': ('Warded Ranks', 'NH_perk_warcasting2_warded_ranks'),
         SKILL + '.enchantedSteel': ('Enchanted Steel', 'NH_perk_warcasting2_enchanted_steel'),
         SKILL + '.grandConvergence': ('Grand Convergence', 'NH_perk_warcasting2_grand_convergence'),
         SKILL + '.unbrokenCommand': ('Unbroken Command', 'NH_perk_warcasting2_unbroken_command')}
RANK_IMAGES = {rank: {role: 'NH_warcasting2_rankart_v5_%s_%s.png' % (rank, role) for role in SIZES} for rank in RANKS}
# Rank art is opaque on the native template; perk icons are transparent foregrounds.
EXPECTED = {'SPRITES/' + name: SIZES[role] for rank in RANKS for role, name in RANK_IMAGES[rank].items()}
for _, stem in PERKS.values():
    EXPECTED.update({'SPRITES/' + stem + '.png': (44, 44), 'SPRITES/' + stem + '_32.png': (32, 32),
                     'SPRITES/' + stem + '.json': None,
                     **{'SPRITES/' + stem + '_' + state + '.png': (44, 44) for state in STATES}})


def warcasting_wave(waves):
    matches = [wave for wave in waves if 'warcastingArtwork' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate Warcasting artwork wave')
    payload = json.dumps(matches[0], sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Warcasting artwork wave changed')
    return matches[0]


def derived_states(native44):
    """The perk importer's button-state recipe (tools/import_new_horizons_perk_art.py)."""
    with Image.open(BytesIO(native44)) as image:
        normal = image.convert('RGBA')
    return {'normal': normal, 'pressed': ImageEnhance.Brightness(normal).enhance(0.82),
            'disabled': ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
            'highlighted': ImageEnhance.Brightness(normal).enhance(1.13)}


def descriptor(stem):
    return {'images': [{'group': 0, 'frame': index, 'file': stem + '_' + state + '.png'}
                       for index, state in enumerate(STATES)]}


def selection_record():
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Warcasting artwork provenance changed')
    return json.loads(payload)


def bindings_present(header, skills):
    """True once any consumer of the new skill exists; from then on every binding must be exact."""
    return SKILL.split(':')[1] in skills or ('"' + SKILL + '.') in header


def validate_bindings(header, skills, module_skills, selected):
    """Rank images in config and mod.json, and each perk bound to its icon exactly once."""
    if ({row['id']: (row['name'], row['icon44']) for row in selected['perkBindings']} != PERKS
            or selected['skill'] != {'id': SKILL, 'name': 'Warcasting', 'images': RANK_IMAGES}):
        raise ValueError('Warcasting binding record changed')
    key = SKILL.split(':')[1]
    for source in (skills, module_skills):
        if key not in source or {rank: source[key][rank]['images'] for rank in RANKS} != RANK_IMAGES:
            raise ValueError('Warcasting rank art is not bound in every role')
    for identity, (_, stem) in PERKS.items():
        if header.count('{"' + identity + '", "' + stem + '"}') != 1 or header.count('"' + identity + '"') != 1:
            raise ValueError('Warcasting perk icon is not bound exactly once')


def validate_image(name, data, row):
    with Image.open(BytesIO(data)) as image:
        image.load()
        if (image.mode, image.size, image.info) != ('RGBA', EXPECTED[name], {}):
            raise ValueError('Warcasting artwork canvas or metadata changed')
        alpha = image.getchannel('A')
        opaque = name.startswith('SPRITES/NH_warcasting2_rankart_')
        if (alpha.getextrema() != ((255, 255) if opaque else (0, 255))
                or [list(image.size), list(alpha.getbbox())] != [row['dimensions'], row['alphaBoundingBox']]):
            raise ValueError('Warcasting artwork transparency changed')
        return image.convert('RGBA').tobytes()


def validate_warcasting_art(wave, entries, reader):
    warcasting_wave([wave])
    selected = selection_record()
    record = {row['resource']: row for row in selected['entries']}
    lookup = {row['resource']: row for row in entries}
    if (len(lookup) != len(entries) or set(wave['resources']) != set(EXPECTED) or set(record) != set(EXPECTED)
            or len(EXPECTED) != 82 or wave['entryCount'] != wave['baselineCount'] + len(EXPECTED)
            or wave['warcastingArtwork'] != [{'resources': wave['resources'],
                                              'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete Warcasting artwork family')
    pixels = {}
    for name, row in record.items():
        data = reader(name)
        digest = hashlib.sha256(data).hexdigest()
        if digest != row['sha256'] or digest != lookup[name]['sha256'] or not row['sourceMasterSha256']:
            raise ValueError('Warcasting artwork bytes changed')
        if EXPECTED[name] is not None:
            pixels[name] = validate_image(name, data, row)
    for _, stem in PERKS.values():
        if json.loads(reader('SPRITES/' + stem + '.json')) != descriptor(stem):
            raise ValueError('Warcasting perk descriptor changed')
        for state, image in derived_states(reader('SPRITES/' + stem + '.png')).items():
            if pixels['SPRITES/' + stem + '_' + state + '.png'] != image.tobytes():
                raise ValueError('Warcasting perk state is not derived from the native icon')
    for role in SIZES:
        if len({pixels['SPRITES/' + RANK_IMAGES[rank][role]] for rank in RANKS}) != len(RANKS):
            raise ValueError('Warcasting ranks are not distinct')
