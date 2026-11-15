"""Finite preserved Water/Ice restoration; no private inputs or invented old pixels."""
import hashlib
import json
import struct
from functools import lru_cache

WAVE_SHA256 = 'd953b24b52fda34c142fe99d2127a374ccb8f0da733ef86c0aacc80ee7bbc3ab'
BASELINE_ARCHIVE = 'cfede2273119faf6b93c785cb18b26fc16092bc49eeb6df7cd69a2b22e92584b'


def validate_wave(wave):
    digest = hashlib.sha256(json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if digest != WAVE_SHA256:
        raise ValueError('Unrecognized Water/Ice restoration wave')


def restoration_wave(waves):
    matches = [wave for wave in waves if 'waterIceRestoration' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate Water/Ice restoration wave')
    validate_wave(matches[0])
    return matches[0]


def historical_payload(wave, replacement, payload):
    """Restore exact old descriptor text; portraits retain a typed replacement proof."""
    validate_wave(wave)
    if replacement not in wave['replacedEntries']:
        raise ValueError('Unknown Water/Ice replacement')
    current, old = replacement['current'], replacement['previous']
    if (len(payload), hashlib.sha256(payload).hexdigest()) != (current['size'], current['sha256']):
        raise ValueError('Water/Ice current payload changed')
    if current['resource'].endswith('.json'):
        payload = replacement['previousJson'].encode()
        if (len(payload), hashlib.sha256(payload).hexdigest()) != (old['size'], old['sha256']):
            raise ValueError('Water/Ice historical descriptor changed')
    return payload


@lru_cache(maxsize=1)
def shipping_wave():
    from .nhart_test_resources import later_art_waves
    return restoration_wave(later_art_waves())


def portrait_replacement_proof(row, payload):
    wave = shipping_wave()
    for replacement in wave['replacedEntries']:
        if row == replacement['previous'] and row['resource'].endswith('.png'):
            historical_payload(wave, replacement, payload)
            return True
    return False


def native_groups(payload):
    """Read bounded DEF group/frame tables; native pixel decoding is a separate gate."""
    if len(payload) < 784:
        raise ValueError('Truncated native DEF')
    _, width, height, count = struct.unpack_from('<4I', payload)
    if (width, height) != (450, 400) or count > 100:
        raise ValueError('Unexpected native DEF canvas/groups')
    cursor, groups = 784, {}
    for _ in range(count):
        group, frames, _, _ = struct.unpack_from('<4I', payload, cursor)
        cursor += 16
        if group in groups or not 0 < frames < 1000 or cursor + frames * 17 > len(payload):
            raise ValueError('Invalid native DEF group')
        offsets = struct.unpack_from('<' + 'I' * frames, payload, cursor + frames * 13)
        if any(offset + 32 > len(payload) for offset in offsets):
            raise ValueError('Invalid native DEF frame offset')
        groups[group] = frames
        cursor += frames * 17
    return groups


def validate_restoration(wave, entries, reader):
    validate_wave(wave)
    selected = {entry['resource']: entry for entry in entries}
    for entry in wave['waterIceRestoration']['sourceEntries']:
        if selected.get(entry['resource']) != entry:
            raise ValueError('Water/Ice selected resource changed')
    for replacement in wave['replacedEntries']:
        if selected.get(replacement['current']['resource']) != replacement['current']:
            raise ValueError('Water/Ice selected provenance changed')
        historical_payload(wave, replacement, reader(replacement['current']['resource']))
    frames = 0
    for kind, canonical, expected in (('water', 'cwelem', 104), ('ice', 'cicee', 173)):
        name = 'SPRITES/elementals/' + kind + '/NH_' + kind + '_singlehex_native.def'
        payload = reader(name)
        entry = selected[name]
        if (len(payload), hashlib.sha256(payload).hexdigest()) != (entry['size'], entry['sha256']):
            raise ValueError('Water/Ice native DEF changed')
        groups = native_groups(payload)
        descriptor = json.loads(reader('SPRITES/' + canonical + '.json'))
        images = descriptor['images']
        if len(images) != expected or len({(row['group'], row['frame']) for row in images}) != expected:
            raise ValueError('Water/Ice logical frame identities changed')
        for row in images:
            if (set(row) != {'group', 'frame', 'defFile', 'defGroup', 'defFrame'}
                    or row['defFile'] != name.removeprefix('SPRITES/')
                    or row['defGroup'] not in groups or not 0 <= row['defFrame'] < groups[row['defGroup']]):
                raise ValueError('Water/Ice native frame alias changed')
        frames += sum(groups.values())
    if frames != 235:
        raise ValueError('Water/Ice native frame inventory changed')
