"""Finite provisional Force Lance selection; no authoring inputs or renderer."""
import hashlib
from io import BytesIO
import json

from PIL import Image

WAVE_SHA256 = 'd4dc5b15687a324b016f7511b04cb557b4ccd8a77bdafa2358348fb6beeabf50'
SELECTION_SHA256 = '6de41a96d30341a8952461b2b146abce7ceec04c951bf41a2c4c535e89b8d48c'
PREFIX = 'SPRITES/newHorizons/forceLance/'


def validate_force_lance_wave(wave):
    digest = hashlib.sha256(json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if digest != WAVE_SHA256:
        raise ValueError('Unrecognized provisional Force Lance wave')


def force_lance_wave(waves):
    matches = [wave for wave in waves if 'forceLanceEffects' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate provisional Force Lance wave')
    validate_force_lance_wave(matches[0])
    return matches[0]


def validate_force_lance(wave, entries, reader, selection_bytes):
    """Verify exact records, seven payloads and native animation/alpha contracts."""
    validate_force_lance_wave(wave)
    if hashlib.sha256(selection_bytes).hexdigest() != SELECTION_SHA256:
        raise ValueError('Force Lance selection provenance changed')
    selection = json.loads(selection_bytes)
    row = wave['forceLanceEffects'][0]
    lookup = {entry['resource']: entry for entry in entries}
    if len(lookup) != len(entries) or selection['entries'] != row['sourceEntries']:
        raise ValueError('Force Lance selected inventory changed')
    if [entry for entry in entries if entry['family'] == 'force-lance-provisional-effects'] != row['sourceEntries']:
        raise ValueError('Force Lance family contains unselected resources')
    for entry in row['sourceEntries']:
        name = entry['resource']
        if lookup.get(name) != entry:
            raise ValueError('Force Lance source provenance changed')
        payload = reader(name)
        if (len(payload), hashlib.sha256(payload).hexdigest()) != (entry['size'], entry['sha256']):
            raise ValueError('Force Lance selected payload changed')
        if name.endswith('.png'):
            _validate_image(payload)
        else:
            _validate_descriptor(name, payload)


def _validate_image(payload):
    image = Image.open(BytesIO(payload))
    image.load()
    if image.mode != 'RGBA' or image.size != (160, 160) or image.info:
        raise ValueError('Force Lance native canvas or metadata changed')
    alpha = image.getchannel('A')
    edges = ((0, 0, 160, 1), (0, 159, 160, 160), (0, 0, 1, 160), (159, 0, 160, 160))
    if alpha.getextrema() != (0, 255) or any(alpha.crop(box).getextrema()[1] for box in edges):
        raise ValueError('Force Lance alpha contract changed')


def _validate_descriptor(name, payload):
    phases = {'strikeImpact.json': ['strike', 'strike', 'impact', 'impact'],
              'hold.json': ['hold'], 'release.json': ['release']}[name.removeprefix(PREFIX)]
    expected = {'images': [{'group': 0, 'frame': i,
                           'file': 'newHorizons/forceLance/' + phase + '.png'}
                          for i, phase in enumerate(phases)]}
    if json.loads(payload) != expected:
        raise ValueError('Force Lance animation or native placement changed')
