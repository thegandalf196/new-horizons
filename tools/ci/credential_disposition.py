# SPDX-License-Identifier: GPL-2.0-or-later
"""Reviewed runtime identifiers: exact finding tuples, never value/file ignores."""
from collections import Counter

import credential_budget as budget
from scanner_runtime import ScannerError

DISPOSITION = 'known-runtime-resource-identifier'
RULE_SHA256 = '9d881e62cdea6c933acbed58dcbaab2c1b6738e906169d00d1de1427ab34a4f9'
FIELDS = ('rule_sha256', 'oid', 'path_sha256', 'start', 'end')
# Two reviewed versions of generation.json and the unchanged runtime manifest.
# These are resource identities, not credentials. No content value is ignored.
BINDINGS = (
    ('3cd2c501cbe0134a168e716e26fc7ae5d63e13a6',
     '4a4d4307f50d49a123e22db8062dde29f2b461f1ec6187b1ff2016ba54b5b51e', 23, 23),
    ('6035cebf590a1e903f1287076475f2877b999f18',
     '4a4d4307f50d49a123e22db8062dde29f2b461f1ec6187b1ff2016ba54b5b51e', 28, 28),
    ('883243419062712a02596568d1e8978f3f7f3ca3',
     '7932490cd15f8c73a561443b2d09adc09933efa45a90d29da17d48d5a30e2666', 6, 6),
)


def records():
    return [dict(zip(FIELDS, (RULE_SHA256, *binding))) for binding in BINDINGS]


def contract():
    return dict(disposition=DISPOSITION, exact_finding_bindings=records(), maximum_reviewed_per_selection=2,
        limits='only reviewed rule/blob/path/line versions; no value, suffix or file ignore; all matching metadata retained; changed tuples require explicit review')


def identity(row):
    if not isinstance(row, dict) or set(row) != set(FIELDS):
        return None
    if (any(type(row[key]) is not str for key in FIELDS[:3])
            or any(type(row[key]) is not int for key in FIELDS[3:])):
        return None
    return tuple(row[key] for key in FIELDS)


def partition(findings, deadline):
    """Preserve every mapped record exactly once; duplicates stay unresolved."""
    budget.tick(deadline)
    if len(findings) > budget.FINDINGS:
        raise ScannerError('scanner-total-finding-limit')
    allowed = {identity(row) for row in records()}
    keys, occurrences, paths = [], Counter(), Counter()
    for row in findings:
        budget.tick(deadline)
        key = identity(row)
        eligible = key if key in allowed else None
        keys.append(eligible)
        if eligible is not None:
            occurrences[eligible] += 1
            paths[eligible[2]] += 1
    unresolved, reviewed = [], []
    for row, key in zip(findings, keys):
        budget.tick(deadline)
        if key is not None and occurrences[key] == 1 and paths[key[2]] == 1:
            reviewed.append(dict(row, disposition=DISPOSITION))
        else:
            unresolved.append(row)
    budget.tick(deadline)
    return unresolved, reviewed, dict(detected=len(findings), reviewed_nonsecret=len(reviewed), unresolved=len(unresolved))


def valid_partition(unresolved, reviewed, counts):
    """CI validates exact disposition records and no dropped-finding accounting."""
    if not isinstance(unresolved, list) or not isinstance(reviewed, list) or not isinstance(counts, dict):
        return False
    if (set(counts) != {'detected', 'reviewed_nonsecret', 'unresolved'}
            or any(type(n) is not int or n < 0 for n in counts.values())
            or not counts['detected'] == counts['reviewed_nonsecret'] + counts['unresolved'] <= budget.FINDINGS
            or counts['reviewed_nonsecret'] != len(reviewed) or counts['unresolved'] != len(unresolved)
            or len(reviewed) > 2):
        return False
    allowed, paths = {identity(row) for row in records()}, set()
    for row in reviewed:
        if (not isinstance(row, dict) or set(row) != {*FIELDS, 'disposition'}
                or row['disposition'] != DISPOSITION):
            return False
        key = identity({field: row[field] for field in FIELDS})
        if key not in allowed or key[2] in paths:
            return False
        paths.add(key[2])
    return True
