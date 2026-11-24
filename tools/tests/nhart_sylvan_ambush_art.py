"""Finite owner-approved Sylvan Tactics 5 (Ambush) artwork (2026-11-23).

Two 44px status icons, three perk icons (native 44/32, four derived 44px button
states and their descriptor) and the four-state Ambush Wait button.
"""
import hashlib
from io import BytesIO
import json
from pathlib import Path
from PIL import Image, ImageEnhance

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/sylvan-ambush-art-selections.json'
SELECTION_SHA256 = 'a6d3592a00ccc982c990e54ff136d490e61aa3d3a49c51a8a7a92931d79ae199'
WAVE_SHA256 = '1e6cd2c6818b6753abd224472934dd0f4a57fbbab6c5c2abfe8dc96b67cad384'
STATES = ('normal', 'pressed', 'disabled', 'highlighted')
PERKS = {'new-horizons:sylvanLuck.forestCover': ('Forest Cover', 'NH_perk_sylvan_tactics_forest_cover'),
         'new-horizons:sylvanLuck.startle': ('Startle', 'NH_perk_sylvan_tactics_startle'),
         'new-horizons:sylvanLuck.apexPredator': ('Apex Predator', 'NH_perk_sylvan_tactics_apex_predator')}
# Status icons fill a 44px bonus card; the Wait button replaces icm006 (48x36, opaque).
EXPECTED = {'SPRITES/NH_status_sylvan_concealed.png': (44, 44),
            'SPRITES/NH_status_sylvan_ambush_strike.png': (44, 44),
            'SPRITES/NH_wait_ambush_button.json': None,
            **{'SPRITES/NH_wait_ambush_' + state + '.png': (48, 36) for state in STATES}}
for _, stem in PERKS.values():
    EXPECTED.update({'SPRITES/' + stem + '.png': (44, 44), 'SPRITES/' + stem + '_32.png': (32, 32),
                     'SPRITES/' + stem + '.json': None,
                     **{'SPRITES/' + stem + '_' + state + '.png': (44, 44) for state in STATES}})


def sylvan_ambush_wave(waves):
    matches = [wave for wave in waves if 'sylvanAmbushArtwork' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate Sylvan Ambush artwork wave')
    payload = json.dumps(matches[0], sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Sylvan Ambush artwork wave changed')
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


def validate_bindings(header, hybrid_header, selected):
    if {row['id']: (row['name'], row['icon44']) for row in selected['perkBindings']} != PERKS:
        raise ValueError('Sylvan Ambush perk binding record changed')
    for identity, (name, stem) in PERKS.items():
        if (header.count('{"' + identity + '", "' + stem + '"}') != 1
                or hybrid_header.count('{"' + identity + '", "' + name + '", "' + stem + '"}') != 1):
            raise ValueError('Sylvan Ambush perk icon is not bound exactly once')


def validate_image(name, data, row):
    with Image.open(BytesIO(data)) as image:
        image.load()
        if (image.mode, image.size, image.info) != ('RGBA', EXPECTED[name], {}):
            raise ValueError('Sylvan Ambush artwork canvas or metadata changed')
        alpha = image.getchannel('A')
        opaque = name.startswith('SPRITES/NH_wait_ambush_')
        if (alpha.getextrema() != ((255, 255) if opaque else (0, 255))
                or [list(image.size), list(alpha.getbbox())] != [row['dimensions'], row['alphaBoundingBox']]):
            raise ValueError('Sylvan Ambush artwork transparency changed')
        return image.convert('RGBA').tobytes()


def validate_sylvan_ambush_art(wave, entries, reader):
    sylvan_ambush_wave([wave])
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Sylvan Ambush artwork provenance changed')
    selected = json.loads(payload)
    record = {row['resource']: row for row in selected['entries']}
    lookup = {row['resource']: row for row in entries}
    if (len(lookup) != len(entries) or set(wave['resources']) != set(EXPECTED) or set(record) != set(EXPECTED)
            or wave['entryCount'] != wave['baselineCount'] + len(EXPECTED)
            or wave['sylvanAmbushArtwork'] != [{'resources': wave['resources'],
                                                'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete Sylvan Ambush artwork family')
    pixels = {}
    for name, row in record.items():
        data = reader(name)
        digest = hashlib.sha256(data).hexdigest()
        if digest != row['sha256'] or digest != lookup[name]['sha256'] or not row['sourceMasterSha256']:
            raise ValueError('Sylvan Ambush artwork bytes changed')
        if EXPECTED[name] is not None:
            pixels[name] = validate_image(name, data, row)
    if json.loads(reader('SPRITES/NH_wait_ambush_button.json')) != descriptor('NH_wait_ambush'):
        raise ValueError('Sylvan Ambush Wait button descriptor changed')
    for _, stem in PERKS.values():
        if json.loads(reader('SPRITES/' + stem + '.json')) != descriptor(stem):
            raise ValueError('Sylvan Ambush perk descriptor changed')
        for state, image in derived_states(reader('SPRITES/' + stem + '.png')).items():
            if pixels['SPRITES/' + stem + '_' + state + '.png'] != image.tobytes():
                raise ValueError('Sylvan Ambush perk state is not derived from the native icon')
    validate_bindings((ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text(),
                      (ROOT / 'client/windows/NewHorizonsSylvanHybridPerkIcons.h').read_text(), selected)
