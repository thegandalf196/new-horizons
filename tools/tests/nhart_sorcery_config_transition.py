"""Exact Sorcery revision before the unchanged historical config proof chain."""
import copy
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = 'Mods/new-horizons/Content/config/spells/newHorizons.json'
RECORD = 'assets/new-horizons/runtime-art-sorcery-config-transition.json'
RECORD_SHA256 = '9e15a51db424790572465e58dbe63f84a23642ae27f1b31605888ee8a3f49df7'


def transition_record():
    payload = (ROOT / RECORD).read_bytes()
    if hashlib.sha256(payload).hexdigest() != RECORD_SHA256:
        raise ValueError('Sorcery artwork config transition metadata changed')
    return json.loads(payload)


def projection(config, excluded, retired=()):
    result = copy.deepcopy(config)
    for pointer in excluded:
        spell, graphics, role = pointer.strip('/').split('/')
        if spell not in result and spell in retired:
            continue
        if spell not in result or graphics not in result[spell] or role not in result[spell][graphics]:
            raise ValueError('Selected artwork graphics pointer changed')
        del result[spell][graphics][role]
    return hashlib.sha256(json.dumps(result, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def before_sorcery(config, path=CONFIG):
    """Restore exact retired definitions for tests, never for runtime loading."""
    if path != CONFIG:
        return copy.deepcopy(config)
    record = transition_record()
    excluded = record['excludedGraphics']
    retired = [row['key'] for row in record['changes'] if 'after' not in row]
    digest = projection(config, excluded, retired)
    if digest == record['historicalProjectionSha256']:
        return copy.deepcopy(config)
    if digest != record['currentProjectionSha256']:
        raise ValueError('Current Sorcery artwork config projection changed')
    historical = copy.deepcopy(config)
    for row in record['changes']:
        key = row['key']
        if 'before' in row:
            historical[key] = copy.deepcopy(row['before'])
        else:
            del historical[key]
    if projection(historical, excluded) != record['historicalProjectionSha256']:
        raise ValueError('Sorcery artwork historical projection changed')
    return historical
