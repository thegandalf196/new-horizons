# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact inherited manifest identities may permit text diagnostics, never a gate."""
import hashlib
import json

import credential_budget as budget
from credential_git import git
from scanner_runtime import ScannerError

ANCESTOR = 'bc0fc2a9f83c9caf0d90575e38ad4b7008b2764a'
BINDINGS = {
    'CI/android/android-release.jks': '7ff98871cf7c1f1f130241aac496f29137efc6cf',
    'CI/android/vcmi-travis.jks': '9d1ac344b4d938d024ccaff760b24b317f3783cd',
}
DISPOSITION = 'known-public-upstream-binary-uninspected'


def contract():
    return dict(mode='explicit-inherited-jks-supported-text-diagnostic', gate_pass=False,
                ancestor=ANCESTOR, uninspected_objects=2, uninspected_aliases=2,
                binding_path_sha256={hashlib.sha256(p.encode()).hexdigest(): oid for p, oid in BINDINGS.items()},
                disposition=DISPOSITION, binary_safety_established=False)


def authorize(selected, deadline):
    """Complete selection and provenance metadata only, before any blob fetch."""
    budget.tick(deadline)
    protected = {oid.encode() for oid in BINDINGS.values()}
    expected = {path: (b'100644', b'blob', oid.encode()) for path, oid in BINDINGS.items()}
    seen = set()
    for path, mode, kind, oid in selected['entries']:
        budget.tick(deadline)
        if path in expected or oid in protected:
            if path not in expected or path in seen or (mode, kind, oid) != expected[path]:
                raise ScannerError('diagnostic-jks-binding-mismatch')
            seen.add(path)
    required = [dict(code='unsupported-relevant-input', oid=oid,
                     path_sha256=hashlib.sha256(path.encode()).hexdigest(), rule_sha256=None,
                     location='manifest-only-no-content-location') for path, oid in BINDINGS.items()]
    canonical = lambda rows: sorted(json.dumps(row, sort_keys=True) for row in rows)
    if (seen != set(expected) or selected['object_format'] != 'sha1'
            or canonical(selected['issues']) != canonical(required)
            or selected['counts']['denied_entries'] != 2 or selected['counts']['presence_entries'] != 0
            or any(alias['oid'].encode() in protected for alias in selected['aliases'])):
        raise ScannerError('diagnostic-jks-binding-mismatch')
    endpoint = selected['commit']
    if endpoint is None:
        endpoint = git(selected['root'], 'rev-parse', '--verify', 'HEAD^{commit}', deadline=deadline).strip().decode('ascii')
    if git(selected['root'], 'merge-base', '--is-ancestor', ANCESTOR, endpoint, deadline=deadline):
        raise ScannerError('diagnostic-jks-provenance-mismatch')
    # Only two fixed ancestor tree entries: never cat-file or stored key bytes.
    original = git(selected['root'], 'ls-tree', '-rz', '--full-tree', ANCESTOR,
                   '--', *sorted(BINDINGS), deadline=deadline)
    exact = b''.join(b'100644 blob ' + BINDINGS[path].encode() + b'\t' + path.encode() + b'\0'
                     for path in sorted(BINDINGS))
    if original != exact:
        raise ScannerError('diagnostic-jks-provenance-mismatch')
    if not selected['aliases']:
        raise ScannerError('zero-credential-text-selected')
    budget.tick(deadline)
    return [dict(issue, disposition=DISPOSITION, inspected=False) for issue in selected['issues']]
