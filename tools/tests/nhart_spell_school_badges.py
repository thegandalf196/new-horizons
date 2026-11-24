"""Finite owner-selected Mage Guild spell school badge medallions (2026-11-24).

SpellSchoolBadge centres one 18x18 medallion on the bottom edge of a native
83x61 spell scroll; a two-school spell shows both side by side. There is one
medallion per New Horizons school and no per-spell art.
"""
import hashlib
from io import BytesIO
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/spell-school-badge-selections.json'
SELECTION_SHA256 = 'f7dbd0fc9083778ba86ddaa15f05a098c392da4c166cb03ee0ebd15671b5a569'
WAVE_SHA256 = 'ee83112826b8a40f7b935ee5271762a611b0888cc0599bf794955f1e5be338ca'
SCHOOLS = ('light', 'nature', 'sorcery', 'havoc', 'shadow', 'chaos')
EXPECTED = {'SPRITES/NH_school_badge_%s.png' % school: 'new-horizons:' + school for school in SCHOOLS}
SIZE = (18, 18)


def school_badge_wave(waves):
    matches = [wave for wave in waves if 'spellSchoolBadges' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate spell school badge wave')
    wave = matches[0]
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Spell school badge wave changed')
    return wave


def validate_school_badges(wave, entries, reader):
    school_badge_wave([wave])
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Spell school badge provenance changed')
    selected = json.loads(payload)
    lookup = {row['resource']: row for row in entries}
    if (len(lookup) != len(entries) or set(wave['resources']) != set(EXPECTED)
            or {row['resource']: row['school'] for row in selected['entries']} != EXPECTED
            or wave['spellSchoolBadges'] != [{'resources': wave['resources'],
                                              'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete spell school badge family')
    for row in selected['entries']:
        name = row['resource']
        data = reader(name)
        digest = hashlib.sha256(data).hexdigest()
        if digest != row['sha256'] or digest != lookup[name]['sha256']:
            raise ValueError('Spell school badge bytes changed')
        with Image.open(BytesIO(data)) as image:
            image.load()
            if (image.mode, image.size, image.info) != ('RGBA', SIZE, {}):
                raise ValueError('Spell school badge canvas or metadata changed')
            alpha = image.getchannel('A')
            if alpha.getextrema() != (0, 255) or list(alpha.getbbox()) != row['alphaBoundingBox']:
                raise ValueError('Spell school badge transparency changed')
