"""Exact missing-art lineage records; no generic future-role acceptance."""
import hashlib
from io import BytesIO
import json
import struct
from PIL import Image

APPROVED_ART_ROLES = {
    ('new-horizons:feint', 'order-button'): '1b22d9db24a08f1cdd34f3fa3f5a52bd42772cd015034ebdf7ab5f4cc389e0e3',
    ('new-horizons:vanguard', 'order-button'): '5802acb539c720331d09f179197cc977d76a8cda969a409653f81b6b8307afe0',
    ('new-horizons:summonLivingArmor', 'spell-symbol'): '89cf5c22e022ee08e40bf199a4b7e63fc6c3d8bc12ccc8cfb4c0cd04735a6ba7',
    ('new-horizons:ghost', 'creature-animation'): '77df0f4718957b62375f93f2f4297be68f404f6815c42a509d771b6da755aaac',
    ('new-horizons:ghost', 'creature-portraits'): '1d871eaa58e4e3a0919308b0eb6c4c9e69eb13877c8192cd4e6951b8afcb91d0',
    ('new-horizons:bugReport', 'interface-icon'): '4ef80fa95ed4961ef5d5f73c52838e1f1dbaf8fc992048858a6b50ccdc63e818',
}
APPROVED_SPECIALTIES = {
    'core:ajit': 'b4d9447eac782c8317d921169eece45bea0eab495812e6b97629cd959bd2b52a',
    'core:bron': '64636cbeb877ae771c65c3cb45e77f845aa6c6cd319bdfae0b53bdf80d644421',
    'core:edric': 'b022183765e7d78b29891b31db811da4632b8b468e41b28942ded6f4392b6cbb',
    'core:fafner': 'd7497ae2fa8fde2698ebdad41d39a0f64bd714260cc1b77a41fb356ea267dd34',
    'core:fiur': '83be1348faf3d767a6f3f926b6779e4cf31965685a07afd1651e1c677eed06e6',
    'core:ignatius': '1a526cef38387c77b69abbdee4fc948d3801d23e5f5e97b111278f2a7cf10b75',
    'core:ivor': 'f703d9f7ba98a511494e49c3e89f95d5232a3f6983f77e793f4adc51f5007c9b',
    'core:tamika': 'ce951a5a9dd64d9e0cb55f6359f7ddbf4db0368e963b9ef018e8edc3cf3db47b',
    'core:vidomina': 'ae9fc25a24ad22994f5c8b030707453bd668a65a7ae0a367bfdd67445deddfbd',
    'core:iona': '1c227d4cc95d59e31c2f2f0414c0dabdd377758ef217cf3e840a387be141f561',
    'core:thane': 'f5b1f471b96adfacdfc0d95151da0f5b7426ecb722fff87116e0456d778e9ea1',
}



def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def _source_entries(row, manifest):
    declared = row['resources']
    entries = row['sourceEntries']
    if len(declared) != len(set(declared)) or len(entries) != len(declared):
        raise ValueError('Missing or duplicate source entries')
    if {entry['resource'] for entry in entries} != set(declared):
        raise ValueError('Source entries do not cover exact role resources')
    for entry in entries:
        if manifest.get(entry['resource']) != entry:
            raise ValueError('Original resource provenance changed')


def _export(payload, facts):
    if len(payload) != facts['size'] or hashlib.sha256(payload).hexdigest() != facts['sha256']:
        raise ValueError('Original lineage payload changed')
    if 'descriptor' in facts:
        if json.loads(payload) != facts['descriptor']:
            raise ValueError('Original animation descriptor changed')
    elif 'nativeHeader' in facts:
        if list(struct.unpack_from('<IIII', payload)) != facts['nativeHeader']:
            raise ValueError('Original creature animation header changed')
    else:
        image = Image.open(BytesIO(payload)); image.load()
        alpha = image.getchannel('A')
        if (list(image.size), image.mode, list(alpha.getextrema()), list(alpha.getbbox()), not bool(image.info)) != (
                facts['dimensions'], facts['mode'], facts['alphaRange'], facts['alphaBounds'], facts['metadataEmpty']):
            raise ValueError('Original PNG export facts changed')


def _ghost_groups(payload, row):
    # Original DEF groups follow the 16-byte header and 256 RGB palette entries.
    offset = 16 + 256 * 3
    groups = {}
    for _ in range(row['groupCount']):
        group, count, _, _ = struct.unpack_from('<IIII', payload, offset)
        if str(group) in groups:
            raise ValueError('Duplicate creature animation group')
        groups[str(group)] = count
        offset += 16 + count * 17  # thirteen filename bytes plus 32-bit frame offset
        if offset > len(payload):
            raise ValueError('Creature animation group exceeds original bytes')
    if groups != row['groupFrameCounts'] or sum(groups.values()) != row['frameCount']:
        raise ValueError('Original creature animation groups changed')


def validate_art_roles(wave, manifest, read_resource):
    seen = set()
    for row in wave.get('artRoles', []):
        identity = (row['id'], row['role'])
        if identity in seen or APPROVED_ART_ROLES.get(identity) != _digest(row):
            raise ValueError('Unreviewed or duplicate historical artwork role')
        seen.add(identity)
        _source_entries(row, manifest)
        if set(row['exports']) != set(row['resources']):
            raise ValueError('Role exports do not cover exact resources')
        for name, facts in row['exports'].items():
            payload = read_resource(name)
            _export(payload, facts)
            if row['role'] == 'creature-animation':
                _ghost_groups(payload, row)


def legacy_specialty_family(row, manifest):
    if 'sourceEntries' not in row:
        if 'family' in row:
            raise ValueError('Unreviewed specialty family override')
        return 'builtin-skills-perks-orders-and-ui'
    if APPROVED_SPECIALTIES.get(row['id']) != _digest(row):
        raise ValueError('Unreviewed historical specialty provenance')
    _source_entries(row, manifest)
    return row['family']
