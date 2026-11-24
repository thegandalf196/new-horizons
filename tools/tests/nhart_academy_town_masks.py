"""Area and border masks of the six animated Academy town structures (2026-11-23).

Hover outlines and click areas follow the moving art: each mask is regenerated
from the union alpha of the structure's 20 shipped animation frames, in the
existing Academy convention (white area with that alpha; a 255,220,64 border
whose alpha is the 5x5 dilation minus the union). Replaced masks stay exact
manifest rows in the wave history with a typed proof, never pixels.
"""
import hashlib
from io import BytesIO
import json
from functools import lru_cache
from pathlib import Path
from PIL import Image, ImageChops, ImageFilter

from .nhart_academy_town_animation import STRUCTURES, descriptor_frames

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/academy-town-animation-mask-selections.json'
SELECTION_SHA256 = '2643378c9dd2e721703d1943899d49464866adafc2333f01cb12b327269d6450'
WAVE_SHA256 = '3b9bcc8acdc7a53848065572c7db00a5ca95181108580737c8d11fd94a160d06'
KINDS = ('area', 'border')


def validate_wave(wave):
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Academy town animation mask wave changed')


def town_mask_wave(waves):
    matches = [wave for wave in waves if 'academyTownAnimationMasks' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate Academy town animation mask wave')
    validate_wave(matches[0])
    return matches[0]


def historical_payload(wave, replacement, payload):
    """Check the shipped replacement; the replaced mask stays a typed proof, never pixels."""
    validate_wave(wave)
    current = replacement['current']
    if (replacement not in wave['replacedEntries']
            or (len(payload), hashlib.sha256(payload).hexdigest()) != (current['size'], current['sha256'])):
        raise ValueError('Academy town animation mask payload changed')
    return payload


@lru_cache(maxsize=1)
def shipping_wave():
    from .nhart_test_resources import later_art_waves
    return town_mask_wave(later_art_waves())


def town_mask_replacement_proof(row, payload):
    wave = shipping_wave()
    for replacement in wave['replacedEntries']:
        if row == replacement['previous']:
            historical_payload(wave, replacement, payload)
            return True
    return False


def _png(image):
    buffer = BytesIO()
    image.save(buffer, format='PNG', optimize=False)
    return buffer.getvalue()


def union_masks(alphas):
    """Area and border masks of the union silhouette, in the Academy mask convention."""
    union = alphas[0]
    for alpha in alphas[1:]:
        union = ImageChops.lighter(union, alpha)
    area = Image.new('RGBA', union.size, (255, 255, 255, 0))
    area.putalpha(union)
    border = Image.new('RGBA', union.size, (255, 220, 64, 0))
    border.putalpha(ImageChops.subtract(union.filter(ImageFilter.MaxFilter(5)), union))
    return {'area': _png(area), 'border': _png(border)}


def expected_masks(resource, stem, reader):
    alphas = []
    for name in descriptor_frames(resource, stem, reader):
        with Image.open(BytesIO(reader('SPRITES/' + name))) as image:
            alphas.append(image.getchannel('A').copy())
    return union_masks(alphas)


def validate_town_masks(wave, entries, reader):
    validate_wave(wave)
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Academy town animation mask provenance changed')
    selected = {row['resource']: row for row in json.loads(payload)['entries']}
    lookup = {row['resource']: row for row in entries}
    replaced = [row['current']['resource'] for row in wave['replacedEntries']]
    names = sorted('SPRITES/NH_academy/town/masks/%s-%s.png' % (value[0], kind)
                   for value in STRUCTURES.values() for kind in KINDS)
    if (wave['resources'] or sorted(replaced) != names or set(selected) != set(names)
            or wave['academyTownAnimationMasks'] != [{'resources': [], 'replaced': replaced,
                                                      'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete Academy town animation mask family')
    for replacement in wave['replacedEntries']:
        previous, current = replacement['previous'], replacement['current']
        row = selected[current['resource']]
        if (lookup.get(current['resource']) != current or previous['source'] != current['source']
                or previous['sha256'] != row['previousSha256'] or current['sha256'] != row['sha256']):
            raise ValueError('Academy town animation mask record changed')
        historical_payload(wave, replacement, reader(current['resource']))
    for resource, (structure, stem, _, _) in STRUCTURES.items():
        for kind, expected in expected_masks(resource, stem, reader).items():
            if reader('SPRITES/NH_academy/town/masks/%s-%s.png' % (structure, kind)) != expected:
                raise ValueError('Academy town %s %s mask is not the union of its frames' % (structure, kind))
