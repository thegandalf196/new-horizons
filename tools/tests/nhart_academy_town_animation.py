"""Finite owner-approved Academy town animation (six structures, 2026-11-23).

The blacksmith, the Workshop and its upgrade, the Grail airship and the Golem
Factory and its upgrade animate with 20 patch-free full frames each. The town
screen draws them with the structure option currentFrameOnly, so no frame is
drawn over frame 0. The wave adds the distinct frames and replaces the six
descriptors and the static images whose rest pose changed. Replaced originals
stay exact manifest rows in the wave history with a typed proof, never pixels.
"""
import hashlib
from io import BytesIO
import json
from functools import lru_cache
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/academy-town-animation-selections.json'
SELECTION_SHA256 = 'db32e32276d28637bddc725f31998ac43c7183585df016799315f2127cc287a5'
WAVE_SHA256 = '5582ace26b51c2f88cffd126c788ec256f019b095c00deb058b92c97459aa9ef'
FRAMES = 20
# animation resource: (structure, static stem, canvas, static rest pose replaced)
STRUCTURES = {
    'NH_ACADEMY_TBTWBLAK': ('blacksmith', 'tbtwblak', (140, 146), True),
    'NH_ACADEMY_TBTWDW_0': ('dwellingLvl1', 'tbtwdw_0', (53, 71), False),
    'NH_ACADEMY_TBTWUP_0': ('dwellingUpLvl1', 'tbtwup_0', (64, 71), False),
    'NH_ACADEMY_TBTWHOLY': ('grail', 'tbtwholy', (90, 218), False),
    'NH_ACADEMY_TBTWDW_2': ('dwellingLvl3', 'tbtwdw_2', (108, 127), True),
    'NH_ACADEMY_TBTWUP_2': ('dwellingUpLvl3', 'tbtwup_2', (108, 127), True),
}
# The blacksmith's static image is its closed-door rest frame; the Golem Factories use the
# artist's separately painted rest pose, pinned by its selection record.
BLACKSMITH_REST_FRAME = 5
ADDED = 110
REPLACED = 9


def validate_wave(wave):
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Academy town animation wave changed')


def town_animation_wave(waves):
    matches = [wave for wave in waves if 'academyTownAnimation' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate Academy town animation wave')
    validate_wave(matches[0])
    return matches[0]


def historical_payload(wave, replacement, payload):
    """Check the shipped replacement; the replaced original stays a typed proof, never pixels."""
    validate_wave(wave)
    current = replacement['current']
    if (replacement not in wave['replacedEntries']
            or (len(payload), hashlib.sha256(payload).hexdigest()) != (current['size'], current['sha256'])):
        raise ValueError('Academy town animation payload changed')
    return payload


@lru_cache(maxsize=1)
def shipping_wave():
    from .nhart_test_resources import later_art_waves
    return town_animation_wave(later_art_waves())


def town_animation_replacement_proof(row, payload):
    wave = shipping_wave()
    for replacement in wave['replacedEntries']:
        if row == replacement['previous']:
            historical_payload(wave, replacement, payload)
            return True
    return False


def _image(data, canvas):
    with Image.open(BytesIO(data)) as image:
        image.load()
        if (image.mode, image.size, image.info) != ('RGBA', canvas, {}):
            raise ValueError('Academy town animation canvas or metadata changed')
        return image.copy()


def descriptor_frames(resource, stem, reader):
    descriptor = json.loads(reader('SPRITES/%s.json' % resource))
    images = descriptor.get('images', [])
    if set(descriptor) != {'images'} or [(row.get('group'), row.get('frame')) for row in images] != [
            (0, index) for index in range(FRAMES)]:
        raise ValueError('Academy town animation descriptor changed its frame layout')
    prefix = 'NH_academy/town/animation/%s/frame-' % stem
    if any(not row['file'].startswith(prefix) or set(row) != {'group', 'frame', 'file'} for row in images):
        raise ValueError('Academy town animation descriptor left its frame folder')
    return [row['file'] for row in images]


def validate_town_animation(wave, entries, reader):
    validate_wave(wave)
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Academy town animation provenance changed')
    record = json.loads(payload)
    selected = {row['resource']: row for row in record['entries']}
    lookup = {row['resource']: row for row in entries}
    replaced = [row['current']['resource'] for row in wave['replacedEntries']]
    if (len(wave['resources']) != ADDED or len(replaced) != REPLACED
            or set(selected) != set(wave['resources']) | set(replaced)
            or wave['academyTownAnimation'] != [{'resources': wave['resources'], 'replaced': replaced,
                                                 'selectionRecordSha256': SELECTION_SHA256}]
            or record['structures'] != [{'structure': value[0], 'animation': key, 'currentFrameOnly': True}
                                        for key, value in STRUCTURES.items()]):
        raise ValueError('Incomplete Academy town animation family')
    for name in wave['resources']:
        if lookup.get(name, {}).get('sha256') != selected[name]['sha256']:
            raise ValueError('Academy town animation frame record changed')
    for replacement in wave['replacedEntries']:
        previous, current = replacement['previous'], replacement['current']
        row = selected[current['resource']]
        if (lookup.get(current['resource']) != current or previous['source'] != current['source']
                or previous['sha256'] != row['previousSha256'] or current['sha256'] != row['sha256']):
            raise ValueError('Academy town animation replacement record changed')
        historical_payload(wave, replacement, reader(current['resource']))
    for resource, (structure, stem, canvas, static_replaced) in STRUCTURES.items():
        files = descriptor_frames(resource, stem, reader)
        frames = []
        for index, name in enumerate(files):
            data = reader('SPRITES/' + name)
            row = selected.get('SPRITES/' + name)
            if (row is None or row['structure'] != structure or 'frame_%02d.png' % index not in row['handoffFrames']
                    or hashlib.sha256(data).hexdigest() != row['sha256']):
                raise ValueError('Academy town animation frame changed')
            frames.append(_image(data, canvas))
        if any(frame.getchannel('A').getextrema() != (0, 255) for frame in frames):
            raise ValueError('Academy town animation frame transparency changed')
        static = 'SPRITES/NH_academy/town/buildings/%s.png' % stem
        if (static in replaced) != static_replaced:
            raise ValueError('Academy town animation static rest pose selection changed')
        static_bytes = reader(static)
        if static_replaced:
            _image(static_bytes, canvas)
        else:
            with Image.open(BytesIO(static_bytes)) as image:
                if (image.mode, image.size) != ('RGBA', canvas):
                    raise ValueError('Academy town animation kept static image changed')
        if resource == 'NH_ACADEMY_TBTWBLAK' and static_bytes != reader('SPRITES/' + files[BLACKSMITH_REST_FRAME]):
            raise ValueError('Academy blacksmith static image is not its rest frame')
