"""Exact creature backdrops and Jousting Immunity icon, retaining older artwork."""
import hashlib
from io import BytesIO
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RECORD = 'assets/new-horizons/living-armor-backdrop-art-selections.json'
RECORD_SHA256 = 'aeff9db8a6e236b70b771acc0b2c666264a1fcebf8cd8294f7184b8241d2d7cf'
WAVE_SHA256 = 'efec88c91f946e9bb46a8b141e650a1571fe66ad11fb84c61cb12d3c0d8e4284'


def backdrop_wave(waves):
    matches = [wave for wave in waves if 'creatureBackdrops' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate Living Armor backdrop wave')
    wave = matches[0]
    if hashlib.sha256(json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()).hexdigest() != WAVE_SHA256:
        raise ValueError('Living Armor backdrop wave changed')
    return wave


def validate_backdrop(wave, entries, reader):
    backdrop_wave([wave])
    payload = (ROOT / RECORD).read_bytes()
    if hashlib.sha256(payload).hexdigest() != RECORD_SHA256:
        raise ValueError('Living Armor backdrop provenance changed')
    selected = json.loads(payload)
    exports = selected['exports']
    expected = {f'SPRITES/NH_living_armor_aetheric_background_{height}.png': (100, height)
                for height in (130, 120)}
    backdrops = sorted(expected)
    ability = 'SPRITES/NH_ability_jousting_immunity.png'
    expected[ability] = (44, 44)
    icons = selected['abilityIcons']
    if (selected['entity'] != 'new-horizons:livingArmor'
            or len(exports) != 2 or {row['resource'] for row in exports} != set(backdrops)
            or len(icons) != 1 or icons[0]['resource'] != ability
            or icons[0]['ability'] != 'CHARGE_IMMUNITY'
            or set(wave['resources']) != set(expected)
            or wave['creatureBackdrops'] != [{'id': selected['entity'],
                 'resources': backdrops, 'selectionRecordSha256': RECORD_SHA256}]
            or wave['abilityIcons'] != [{'id': 'CHARGE_IMMUNITY', 'resources': [ability],
                                        'selectionRecordSha256': RECORD_SHA256}]):
        raise ValueError('Living Armor backdrop native roles changed')
    lookup = {row['resource']: row for row in entries}
    images = {}
    for row in exports + icons:
        name = row['resource']
        data = reader(name)
        if hashlib.sha256(data).hexdigest() != row['sha256'] or lookup[name]['sha256'] != row['sha256']:
            raise ValueError('Living Armor backdrop bytes changed')
        image = Image.open(BytesIO(data))
        image.load()
        if (image.mode, image.size, image.info) != ('RGBA', expected[name], {}):
            raise ValueError('Living Armor backdrop canvas/opacity changed')
        alpha = image.getchannel('A')
        if name == ability:
            if alpha.getextrema() != (0, 255) or alpha.getbbox() != (2, 2, 42, 42):
                raise ValueError('Jousting Immunity icon transparency changed')
        elif alpha.getextrema() != (255, 255):
            raise ValueError('Living Armor backdrop opacity changed')
        images[image.height] = image
    if images[130].crop((0, 0, 100, 120)).tobytes() != images[120].tobytes():
        raise ValueError('Living Armor portrait crop relationship changed')
