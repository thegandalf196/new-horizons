"""Finite source-consumer projections; historical artwork approvals stay exact."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORD = 'assets/new-horizons/runtime-art-source-transitions.json'
RECORD_SHA256 = 'eb2e29b7d0bda8d18b2623d5331dee43fbb2a195f9f173879607321f9fb6da21'


def source_records():
    payload = (ROOT / RECORD).read_bytes()
    if hashlib.sha256(payload).hexdigest() != RECORD_SHA256:
        raise ValueError('Artwork source transition metadata changed')
    return json.loads(payload)['files']


def historical_source(path, source):
    """Reverse only selected exact blocks, retaining unrelated source bytes."""
    record = source_records().get(path)
    if record is None:
        raise ValueError('Unknown artwork source transition')
    if hashlib.sha256(source.encode()).hexdigest() == record['historicalSha256']:
        return source
    historical = source
    for change in reversed(record['changes']):
        if historical.count(change['after']) != 1:
            raise ValueError('Current artwork source binding changed')
        historical = historical.replace(change['after'], change['before'], 1)
    return historical


def read_historical_source(path):
    source = (ROOT / path).read_text()
    record = source_records()[path]
    if hashlib.sha256(source.encode()).hexdigest() != record['currentSha256']:
        raise ValueError('Unrecognized current artwork consumer')
    historical = historical_source(path, source)
    if hashlib.sha256(historical.encode()).hexdigest() != record['historicalSha256']:
        raise ValueError('Artwork consumer historical projection changed')
    return historical
