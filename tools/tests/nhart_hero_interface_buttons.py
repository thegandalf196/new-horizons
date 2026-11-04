"""Exact selected hero information and temporary-effect button contracts."""
import hashlib
from io import BytesIO
import json

from PIL import Image


def validate_hero_interface_buttons(records, manifest, read_resource):
    if not records:
        return
    if len(records) != 1:
        raise ValueError('Expected one selected hero interface button family')
    row = records[0]
    if (row['id'], row['family']) != ('new-horizons:heroInterfaceButtons', 'hero-interface-buttons'):
        raise ValueError('Unexpected hero interface family')
    states = ('normal', 'pressed', 'disabled', 'highlighted')
    expected = {'SPRITES/NH_hero_temporary_effects_icon.png'}
    for stem in ('NH_info_button', 'NH_info_button_small'):
        expected.add('SPRITES/' + stem + '.json')
        expected.update('SPRITES/' + stem + '_' + state + '.png' for state in states)
    if len(row['resources']) != 11 or set(row['resources']) != expected:
        raise ValueError('Selected family requires exactly eleven resources')
    if (row['infoCanvas'], row['compactInfoCanvas'], row['temporaryEffectsCanvas'],
            row['infoStates']) != ([22, 22], [16, 16], [24, 24], list(states)):
        raise ValueError('Hero button native canvas/state contract changed')
    source_entries = {entry['resource']: entry for entry in row['sourceEntries']}
    if len(row['sourceEntries']) != 11 or set(source_entries) != expected:
        raise ValueError('Missing selected source provenance')
    for name in expected:
        entry = source_entries[name]
        payload = read_resource(name)
        if manifest.get(name) != entry or len(payload) != entry['size'] or hashlib.sha256(payload).hexdigest() != entry['sha256']:
            raise ValueError('Selected hero resource bytes or provenance changed')
        if name.endswith('.json'):
            stem = name.removeprefix('SPRITES/').removesuffix('.json')
            descriptor = {'images': [{'group': 0, 'frame': i, 'file': stem + '_' + state + '.png'}
                                     for i, state in enumerate(states)]}
            if json.loads(payload) != descriptor:
                raise ValueError('Hero info animation frame order changed')
        else:
            image = Image.open(BytesIO(payload)); image.load()
            size = 24 if 'effects' in name else 16 if 'small' in name else 22
            if image.size != (size, size) or image.mode != 'RGBA' or image.info or image.getchannel('A').getextrema() != (0, 255):
                raise ValueError('Hero symbol dimensions, transparency or metadata changed')
    for stem in ('NH_info_button', 'NH_info_button_small'):
        if len({source_entries['SPRITES/' + stem + '_' + state + '.png']['sha256'] for state in states}) != 4:
            raise ValueError('Hero button interaction states must differ')
