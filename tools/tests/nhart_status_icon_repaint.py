"""Finite owner-approved repaint of ten 50px creature-trait status icons (2026-11-23).

The repaint replaces payloads under unchanged resource names. The flat originals
are not reconstructed: their manifest rows stay exact in the wave history and a
typed proof ties each one to its recorded current replacement.
"""
import hashlib
from io import BytesIO
import json
from functools import lru_cache
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/status-icon-repaint-selections.json'
SELECTION_SHA256 = '72281932c12c48f10c769488aeb4a2a2462c5fb41007d494981aa9750aa4249c'
WAVE_SHA256 = '1156d7f6355a09f097695de87f077087a735a9ca1ca88f349bfedba47cb63c32'
STEMS = ('adjacent', 'breath', 'flying', 'noRetaliation', 'regeneration', 'resistance',
         'shooter', 'siege', 'undead', 'unlimitedRetaliations')
RESOURCES = tuple('SPRITES/NH_status_' + stem + '_50.png' for stem in STEMS)


def validate_wave(wave):
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Status icon repaint wave changed')


def repaint_wave(waves):
    matches = [wave for wave in waves if 'statusIconRepaint' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate status icon repaint wave')
    validate_wave(matches[0])
    return matches[0]


def historical_payload(wave, replacement, payload):
    """Check the shipped replacement; the flat original stays a typed proof, never pixels."""
    validate_wave(wave)
    current = replacement['current']
    if (replacement not in wave['replacedEntries'] or current['resource'] not in RESOURCES
            or (len(payload), hashlib.sha256(payload).hexdigest()) != (current['size'], current['sha256'])):
        raise ValueError('Status icon repaint payload changed')
    return payload


@lru_cache(maxsize=1)
def shipping_wave():
    from .nhart_test_resources import later_art_waves
    return repaint_wave(later_art_waves())


def repaint_replacement_proof(row, payload):
    wave = shipping_wave()
    for replacement in wave['replacedEntries']:
        if row == replacement['previous']:
            historical_payload(wave, replacement, payload)
            return True
    return False


def validate_repaint(wave, entries, reader):
    validate_wave(wave)
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Status icon repaint provenance changed')
    selected = {row['resource']: row for row in json.loads(payload)['entries']}
    lookup = {row['resource']: row for row in entries}
    replaced = [row['current']['resource'] for row in wave['replacedEntries']]
    if (wave['resources'] or sorted(replaced) != sorted(RESOURCES) or set(selected) != set(RESOURCES)
            or wave['statusIconRepaint'] != [{'replaced': replaced, 'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete status icon repaint family')
    for replacement in wave['replacedEntries']:
        previous, current = replacement['previous'], replacement['current']
        name = current['resource']
        row = selected[name]
        if (lookup.get(name) != current or previous['source'] != current['source']
                or previous['sha256'] != row['previousSha256'] or current['sha256'] != row['sha256']):
            raise ValueError('Status icon repaint record changed')
        data = historical_payload(wave, replacement, reader(name))
        with Image.open(BytesIO(data)) as image:
            image.load()
            if (image.mode, image.size, image.info) != ('RGBA', (50, 50), {}):
                raise ValueError('Status icon repaint canvas or metadata changed')
            alpha = image.getchannel('A')
            if alpha.getextrema() != (0, 255) or list(alpha.getbbox()) != row['alphaBoundingBox']:
                raise ValueError('Status icon repaint transparency changed')
