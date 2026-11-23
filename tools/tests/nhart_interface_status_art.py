"""Finite owner-approved hero lean glyphs and battle status icons (2026-11-23)."""
import hashlib
from io import BytesIO
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/interface-status-art-selections.json'
SELECTION_SHA256 = '6dbc8c031e7cc17177ce4a8220cd1ac42b61d10b9190f3805d6b5d92e0e3f253'
WAVE_SHA256 = '44b12ba1d468a2ef9c649ba3031ce60f387452e630e2be6f5f882360f33f47e3'
# Inline lean glyphs share a FONT_SMALL text row; status icons fill a 44px bonus card.
EXPECTED = {
    'SPRITES/NH_hero_lean_might.png': (16, 16),
    'SPRITES/NH_hero_lean_magic.png': (16, 16),
    'SPRITES/NH_status_frost_mantle_cloak.png': (44, 44),
    'SPRITES/NH_status_frost_mantle_chill.png': (44, 44),
    'SPRITES/NH_status_brittle.png': (44, 44),
    'SPRITES/NH_status_living_remedy.png': (44, 44),
}


def interface_status_wave(waves):
    matches = [wave for wave in waves if 'interfaceStatusArtwork' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate interface/status artwork wave')
    wave = matches[0]
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Interface/status artwork wave changed')
    return wave


def validate_interface_status_art(wave, entries, reader):
    interface_status_wave([wave])
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Interface/status artwork provenance changed')
    selected = json.loads(payload)
    lookup = {row['resource']: row for row in entries}
    if (len(lookup) != len(entries) or set(wave['resources']) != set(EXPECTED)
            or {row['resource'] for row in selected['entries']} != set(EXPECTED)
            or wave['interfaceStatusArtwork'] != [{'resources': wave['resources'],
                                                   'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete interface/status artwork family')
    for row in selected['entries']:
        name = row['resource']
        data = reader(name)
        digest = hashlib.sha256(data).hexdigest()
        if digest != row['sha256'] or digest != lookup[name]['sha256']:
            raise ValueError('Interface/status artwork bytes changed')
        with Image.open(BytesIO(data)) as image:
            image.load()
            if (image.mode, image.size, image.info) != ('RGBA', EXPECTED[name], {}):
                raise ValueError('Interface/status artwork canvas or metadata changed')
            alpha = image.getchannel('A')
            if alpha.getextrema() != (0, 255) or list(alpha.getbbox()) != row['alphaBoundingBox']:
                raise ValueError('Interface/status artwork transparency changed')
