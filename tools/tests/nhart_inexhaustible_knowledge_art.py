"""Owner-selected specialty icon for Theodorus's Inexhaustible Knowledge (2026-11-25).

One new hero specialty artwork stem in the form every other specialty uses:
native 44x44 and 32x32 transparent icons with a 2px clear border, each with a
group 0 frame 0 descriptor. The code binding to the specialty is a separate
change owned by the specialty's presentation.
"""
import hashlib
from io import BytesIO
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SELECTION = 'assets/new-horizons/inexhaustible-knowledge-specialty-selection.json'
SELECTION_SHA256 = '19342735f253606263400e99e37a7c7fa6486c5f43b768166ac7266cc10a5778'
WAVE_SHA256 = '108b9280c54b9a40ba71fef61ccefbf2c3c9d2775b7c7067d4942bb5afdf22a0'
KEY = 'inexhaustibleKnowledgeSpecialty'
FAMILY = 'hero-specialty-icons'
STEM = 'newHorizons/heroSpecialties/TheodorusInexhaustibleKnowledge'
SIZES = (44, 32)
IMAGES = {'SPRITES/%s/icon-%d.png' % (STEM, size): size for size in SIZES}
DESCRIPTORS = {'SPRITES/%s/%d.json' % (STEM, size): size for size in SIZES}
EXPECTED = set(IMAGES) | set(DESCRIPTORS)


def inexhaustible_knowledge_wave(waves):
    matches = [wave for wave in waves if KEY in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate Inexhaustible Knowledge specialty wave')
    wave = matches[0]
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Inexhaustible Knowledge specialty wave changed')
    return wave


def descriptor(size):
    return {'images': [{'group': 0, 'frame': 0, 'file': '%s/icon-%d.png' % (STEM, size)}]}


def _check_icon(data, size, row):
    with Image.open(BytesIO(data)) as image:
        image.load()
        if (image.mode, image.size, image.info) != ('RGBA', (size, size), {}):
            raise ValueError('Inexhaustible Knowledge icon canvas or metadata changed')
        alpha = image.getchannel('A')
        border = [(0, 0, size, 2), (0, size - 2, size, size), (0, 0, 2, size), (size - 2, 0, size, size)]
        if (alpha.getextrema() != (0, 255) or list(alpha.getbbox()) != row['alphaBoundingBox']
                or any(alpha.crop(box).getextrema() != (0, 0) for box in border)):
            raise ValueError('Inexhaustible Knowledge icon transparency changed')


def validate_inexhaustible_knowledge(wave, entries, reader):
    inexhaustible_knowledge_wave([wave])
    payload = (ROOT / SELECTION).read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTION_SHA256:
        raise ValueError('Inexhaustible Knowledge specialty provenance changed')
    selected = json.loads(payload)
    record = {row['resource']: row for row in selected['entries']}
    lookup = {row['resource']: row for row in entries}
    if (len(lookup) != len(entries) or set(wave['resources']) != EXPECTED or set(record) != EXPECTED
            or wave['entryCount'] != wave['baselineCount'] + len(EXPECTED) or selected['artwork'] != STEM
            or wave[KEY] != [{'resources': wave['resources'], 'selectionRecordSha256': SELECTION_SHA256}]):
        raise ValueError('Incomplete Inexhaustible Knowledge specialty family')
    for name, row in record.items():
        data = reader(name)
        digest = hashlib.sha256(data).hexdigest()
        meta = lookup.get(name, {})
        if (digest != row['sha256'] or digest != meta.get('sha256') or meta.get('family') != FAMILY
                or meta.get('selection') != selected['selection'] or meta.get('approval') != selected['approval']):
            raise ValueError('Inexhaustible Knowledge specialty bytes changed')
        if name in IMAGES:
            _check_icon(data, IMAGES[name], row)
        elif json.loads(data) != descriptor(DESCRIPTORS[name]):
            raise ValueError('Inexhaustible Knowledge descriptor changed')
