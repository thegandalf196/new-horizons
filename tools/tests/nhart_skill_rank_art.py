"""Exact selected rank artwork and inherited native-template provenance."""
import hashlib
from io import BytesIO
import json
from PIL import Image

WAVE_SHA256 = 'f57a91cc24a0f5ec86f9bc2e9a65dc3bfaa4654a4227c15b9d9536d146fad02f'
SELECTION_SHA256 = '45abaf373c6e3f33b9a03eb4ad82905f76ee7d12454b1ede43a4688d817b04fa'
SKILLS = ('battlecraft', 'recruitment', 'authority', 'warcasting', 'divineMandate',
          'sylvanLuck', 'shroudOfMalassa', 'demonicGating', 'bloodrage',
          'bulwarkOfTheMire', 'elementalRebirth')
RANKS = ('basic', 'advanced', 'expert')
SIZES = {'small': (32, 32), 'medium': (44, 44),
         'large': (82, 93), 'scenarioBonus': (58, 64)}


def skill_rank_wave(waves):
    matches = [wave for wave in waves if 'skillRankArtwork' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate selected skill-rank wave')
    wave = matches[0]
    digest = hashlib.sha256(json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if digest != WAVE_SHA256:
        raise ValueError('Selected skill-rank wave changed')
    return wave


def validate_skill_rank_art(wave, entries, reader, selection_bytes):
    skill_rank_wave([wave])
    if hashlib.sha256(selection_bytes).hexdigest() != SELECTION_SHA256:
        raise ValueError('Skill-rank selection provenance changed')
    selection = json.loads(selection_bytes)
    row = wave['skillRankArtwork'][0]
    expected = {f'SPRITES/NH_{skill}_rankart_v2_{rank}_{role}.png'
                for skill in SKILLS for rank in RANKS for role in SIZES}
    if (set(wave['resources']) != expected or len(wave['resources']) != 132
            or row['resources'] != wave['resources']
            or row['selectionRecordSha256'] != SELECTION_SHA256
            or selection['sourceEntries'] != row['sourceEntries']):
        raise ValueError('Skill-rank selected inventory changed')
    lookup = {entry['resource']: entry for entry in entries}
    exports = {record['resource']: record for record in selection['exports']}
    if len(lookup) != len(entries) or set(exports) != expected:
        raise ValueError('Duplicate or incomplete skill-rank exports')
    for entry in row['sourceEntries']:
        name = entry['resource']
        if lookup.get(name) != entry:
            raise ValueError('Skill-rank source provenance changed')
        payload = reader(name)
        if (len(payload), hashlib.sha256(payload).hexdigest()) != (entry['size'], entry['sha256']):
            raise ValueError('Selected skill-rank payload changed')
        image = Image.open(BytesIO(payload))
        image.load()
        facts = exports[name]
        if (image.size, image.mode, image.info) != (SIZES[facts['role']], 'RGBA', {}):
            raise ValueError('Skill-rank native canvas or metadata changed')
