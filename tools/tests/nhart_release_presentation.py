"""Finite selected artwork successor and exact presentation-only config reversal."""
import copy
import hashlib
from io import BytesIO
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/release-art-selections.json'
SELECTION_SHA256 = 'f82cc78f8ade76a3a70c8e4a3ccf63c24e6cb04d5bbb364c125d595663343eeb'
WAVE_SHA256 = '9a5fd757ee19926fae696025868a776c2b73bf61dd8d986f9568db1bc368a2b6'
TRANSITION = 'assets/new-horizons/runtime-art-presentation-config-transition.json'
TRANSITION_SHA256 = '70cf63829fb40702cfec11a3bbe8572b410bb4a45b8693c0cb674fe4134067b1'


def pinned_record(path, expected):
    payload = (ROOT / path).read_bytes()
    if hashlib.sha256(payload).hexdigest() != expected:
        raise ValueError('Selected presentation provenance changed')
    return json.loads(payload)


def presentation_wave(waves):
    matches = [wave for wave in waves if 'releaseArtwork' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate presentation artwork wave')
    wave = matches[0]
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Selected presentation artwork wave changed')
    return wave


def validate_presentation_art(wave, entries, reader):
    presentation_wave([wave])
    selected = pinned_record(SELECTION, SELECTION_SHA256)
    lookup = {row['resource']: row for row in entries}
    if (len(lookup) != len(entries) or len(selected['entries']) != 29
            or {row['resource'] for row in selected['entries']} != set(wave['resources'])
            or wave['releaseArtwork'] != [{'resources': wave['resources'],
                                          'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete presentation resource family')
    for row in selected['entries']:
        payload = reader(row['resource'])
        digest = hashlib.sha256(payload).hexdigest()
        if digest != row['sha256'] or digest != lookup[row['resource']]['sha256']:
            raise ValueError('Selected presentation bytes changed')
        if row['resource'].endswith('.png'):
            with Image.open(BytesIO(payload)) as image:
                if (image.mode, list(image.size), image.info) != ('RGBA', row['dimensions'], {}):
                    raise ValueError('Selected presentation dimensions or metadata changed')


def before_presentation(config):
    """Undo only the exact accepted art/cast delta, never ignore unknown changes."""
    changes = pinned_record(TRANSITION, TRANSITION_SHA256)['changes']
    result = copy.deepcopy(config)
    # Retired pre-Sorcery rules have neither of the two new hero spells.
    if all(key not in result for key in ('forceLance', 'forcedRecall')):
        return result
    current = historical = 0
    for change in changes:
        node = result.get(change['spell'])
        if node is None:
            raise ValueError('Missing presentation spell')
        for key in change['field'][:-1]:
            node = node.get(key, {})
        key = change['field'][-1]
        if key in node and node[key] == change['after']:
            current += 1
            if 'before' in change:
                node[key] = copy.deepcopy(change['before'])
            else:
                del node[key]
        elif (key in node and 'before' in change and node[key] == change['before']
              or key not in node and 'before' not in change):
            historical += 1
        else:
            raise ValueError('Unrecorded presentation config value')
    if current and historical:
        raise ValueError('Mixed presentation config revision')
    return result
