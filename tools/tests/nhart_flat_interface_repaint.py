"""Finite owner-approved repaint of 43 flat interface icons (2026-11-23).

Quick save/load (32 and 64x32), frameless Spells/Cancel, the hero growth 32 px
attribute icons and Movement, the growth entry button, the artillery masteries
and Long Reach keep their resource names, sizes and descriptors. The flat
originals stay exact manifest rows in the wave history with a typed proof.
"""
import hashlib
from io import BytesIO
import json
from functools import lru_cache
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/flat-interface-repaint-selections.json'
SELECTION_SHA256 = '9bde8703e117d3772db7b1c4dbebad1a10cc143cbe5ff0d793eec21d4ba1c2c1'
WAVE_SHA256 = '1440f778e0629421440e03a5adb9d34d2c76ccf5250fd4183f8bc5a5d3fb0bda'
COUNT = 43


def validate_wave(wave):
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Flat interface repaint wave changed')


def flat_repaint_wave(waves):
    matches = [wave for wave in waves if 'flatInterfaceRepaint' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate flat interface repaint wave')
    validate_wave(matches[0])
    return matches[0]


def historical_payload(wave, replacement, payload):
    """Check the shipped replacement; the flat original stays a typed proof, never pixels."""
    validate_wave(wave)
    current = replacement['current']
    if (replacement not in wave['replacedEntries']
            or (len(payload), hashlib.sha256(payload).hexdigest()) != (current['size'], current['sha256'])):
        raise ValueError('Flat interface repaint payload changed')
    return payload


@lru_cache(maxsize=1)
def shipping_wave():
    from .nhart_test_resources import later_art_waves
    return flat_repaint_wave(later_art_waves())


def superseding_replacement(name):
    """The shipped repaint that replaced `name`, if any (for older wave validators)."""
    return next((row for row in shipping_wave()['replacedEntries'] if row['current']['resource'] == name), None)


def flat_replacement_proof(row, payload):
    wave = shipping_wave()
    for replacement in wave['replacedEntries']:
        if row == replacement['previous']:
            historical_payload(wave, replacement, payload)
            return True
    return False


def validate_flat_repaint(wave, entries, reader):
    validate_wave(wave)
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Flat interface repaint provenance changed')
    selected = {row['resource']: row for row in json.loads(payload)['entries']}
    lookup = {row['resource']: row for row in entries}
    replaced = [row['current']['resource'] for row in wave['replacedEntries']]
    if (wave['resources'] or len(replaced) != COUNT or set(replaced) != set(selected)
            or any(not name.endswith('.png') for name in replaced)
            or wave['flatInterfaceRepaint'] != [{'replaced': replaced, 'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete flat interface repaint family')
    for replacement in wave['replacedEntries']:
        previous, current = replacement['previous'], replacement['current']
        row = selected[current['resource']]
        if (previous['source'] != current['source']
                or previous['sha256'] != row['previousSha256'] or current['sha256'] != row['sha256']):
            raise ValueError('Flat interface repaint record changed')
        if current['resource'] not in lookup:
            # A later owner-directed retirement removed this repaint; only its exact row remains.
            from .nhart_retired_interface_icons import retired_row_proof
            if not retired_row_proof(current):
                raise ValueError('Flat interface repaint disappeared without a retirement record')
            continue
        if lookup[current['resource']] != current:
            raise ValueError('Flat interface repaint record changed')
        data = historical_payload(wave, replacement, reader(current['resource']))
        with Image.open(BytesIO(data)) as image:
            image.load()
            if (image.mode, list(image.size), image.info) != ('RGBA', row['dimensions'], {}):
                raise ValueError('Flat interface repaint canvas or metadata changed')
            # Disabled frameless states are deliberately translucent, so only the bounds are pinned.
            if list(image.getchannel('A').getbbox()) != row['alphaBoundingBox']:
                raise ValueError('Flat interface repaint transparency changed')
