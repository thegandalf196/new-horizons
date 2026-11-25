"""Repainted Academy Town Hall icons for the animated buildings (2026-11-24).

The blacksmith (with its forge smoke), the Golem Factory and the upgraded Golem
Factory changed with the Academy town animation, so their Town Hall
build-window icons are recomposed from the selected building art over the
approved Academy landscape. The wave only replaces those three 150x70 opaque
icons; replaced icons stay exact manifest rows in the wave history with a typed
proof, never pixels.
"""
import hashlib
from io import BytesIO
import json
from functools import lru_cache
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/academy-hall-icon-selections.json'
SELECTION_SHA256 = 'a3e09da7c724eda3460533b27247c97c98b00feb030ea6b4572b3c893fbeb1b5'
WAVE_SHA256 = '01a09ae9690a3fd1fde00152d6a128bf13d9b8fe5b6dafcce77dd510fd8e8ebb'
SIZE = (150, 70)
# hall icon: (building, static building sprite whose selected art it shows)
ICONS = {
    'SPRITES/NH_academy/ui/hall/frame-016.png': ('blacksmith', 'tbtwblak'),
    'SPRITES/NH_academy/ui/hall/frame-032.png': ('dwellingLvl3', 'tbtwdw_2'),
    'SPRITES/NH_academy/ui/hall/frame-039.png': ('dwellingUpLvl3', 'tbtwup_2'),
}


def validate_wave(wave):
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Academy hall icon wave changed')


def hall_icon_wave(waves):
    matches = [wave for wave in waves if 'academyHallIcons' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate Academy hall icon wave')
    validate_wave(matches[0])
    return matches[0]


def historical_payload(wave, replacement, payload):
    """Check the shipped replacement; the replaced icon stays a typed proof, never pixels."""
    validate_wave(wave)
    current = replacement['current']
    if (replacement not in wave['replacedEntries']
            or (len(payload), hashlib.sha256(payload).hexdigest()) != (current['size'], current['sha256'])):
        raise ValueError('Academy hall icon payload changed')
    return payload


@lru_cache(maxsize=1)
def shipping_wave():
    from .nhart_test_resources import later_art_waves
    return hall_icon_wave(later_art_waves())


def hall_icon_replacement_proof(row, payload):
    wave = shipping_wave()
    for replacement in wave['replacedEntries']:
        if row == replacement['previous']:
            historical_payload(wave, replacement, payload)
            return True
    return False


def _check_icon(data):
    with Image.open(BytesIO(data)) as image:
        image.load()
        if (image.mode, image.size, image.info) != ('RGBA', SIZE, {}):
            raise ValueError('Academy hall icon canvas or metadata changed')
        if image.getchannel('A').getextrema() != (255, 255):
            raise ValueError('Academy hall icon is no longer opaque')


def validate_hall_icons(wave, entries, reader):
    validate_wave(wave)
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Academy hall icon provenance changed')
    selected = {row['resource']: row for row in json.loads(payload)['entries']}
    lookup = {row['resource']: row for row in entries}
    replaced = [row['current']['resource'] for row in wave['replacedEntries']]
    if (wave['resources'] or replaced != sorted(ICONS) or set(selected) != set(ICONS)
            or wave['entryCount'] != wave['baselineCount']
            or wave['academyHallIcons'] != [{'resources': [], 'replaced': replaced,
                                             'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete Academy hall icon family')
    for replacement in wave['replacedEntries']:
        previous, current = replacement['previous'], replacement['current']
        row = selected[current['resource']]
        building, stem = ICONS[current['resource']]
        art = lookup.get('SPRITES/NH_academy/town/buildings/%s.png' % stem, {})
        if (lookup.get(current['resource']) != current or previous['source'] != current['source']
                or previous['sha256'] != row['previousSha256'] or current['sha256'] != row['sha256']
                or row['building'] != building or row['buildingArt'] != art.get('resource')
                or row['buildingArtSha256'] != art.get('sha256')):
            raise ValueError('Academy hall icon record changed')
        data = reader(current['resource'])
        historical_payload(wave, replacement, data)
        _check_icon(data)
