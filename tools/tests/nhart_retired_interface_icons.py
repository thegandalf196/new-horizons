"""Finite owner-approved retirement of 59 unused flat interface icons (2026-11-23).

Retired rows leave the pack; their exact manifest rows stay in the wave history
so earlier baselines still verify. Their bytes are preserved privately, so the
history proof for a retired row is its exact record, never a payload.
"""
import hashlib
import json
from functools import lru_cache
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORD = 'assets/new-horizons/retired-interface-icons.json'
RECORD_SHA256 = 'b2d74360eaeb78bc2d279e5d66825ceb6be7c8627f54ebba1879f734fe98c4c2'
WAVE_SHA256 = '490a31612fd56f812ff27790ae38daeda8d6a727c3ce2a299b6df7d3acfc340c'
COUNT = 59


def validate_wave(wave):
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Retired interface icon wave changed')


def retirement_wave(waves):
    matches = [wave for wave in waves if 'retiredInterfaceIcons' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate retired interface icon wave')
    validate_wave(matches[0])
    return matches[0]


@lru_cache(maxsize=1)
def shipping_wave():
    from .nhart_test_resources import later_art_waves
    return retirement_wave(later_art_waves())


def retired_row_proof(row):
    """True for an exact retired manifest row, or one whose recorded successors were retired."""
    from .nhart_test_resources import later_art_waves
    waves = later_art_waves()
    successors = {replacement['previous']['resource']: [] for wave in waves for replacement in wave.get('replacedEntries', [])}
    for wave in waves:
        for replacement in wave.get('replacedEntries', []):
            successors[replacement['previous']['resource']].append(replacement)
    retired = [entry for wave in waves for entry in wave.get('retiredEntries', [])]
    seen = 0
    while row not in retired:
        step = next((r['current'] for r in successors.get(row['resource'], []) if r['previous'] == row), None)
        if step is None or seen > len(waves):
            return False
        row, seen = step, seen + 1
    return True


def retired_count(waves):
    return sum(len(wave.get('retiredEntries', [])) for wave in waves)


def validate_retirement(wave, entries, index):
    validate_wave(wave)
    payload = (ROOT / RECORD).read_bytes()
    if hashlib.sha256(payload).hexdigest() != RECORD_SHA256:
        raise ValueError('Retired interface icon record changed')
    record = {row['resource']: row for row in json.loads(payload)['entries']}
    rows = wave['retiredEntries']
    names = [row['resource'] for row in rows]
    current = {row['resource'] for row in entries}
    if (wave['resources'] or len(rows) != COUNT or set(names) != set(record)
            or wave['entryCount'] != wave['baselineCount'] - COUNT
            or wave['retiredInterfaceIcons'] != [{'retired': names, 'selectionRecordSha256': RECORD_SHA256}]
            or current & set(names) or any(name in index for name in names)):
        raise ValueError('Retired interface icons are incomplete or still shipped')
    for row in rows:
        kept = record[row['resource']]
        if (row['sha256'], row['size']) != (kept['sha256'], kept['size']) or not kept['reason']:
            raise ValueError('Retired interface icon record mismatch')
