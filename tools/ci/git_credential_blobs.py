# SPDX-License-Identifier: GPL-2.0-or-later
"""Credential selection separate from unchanged finite R1/R2 text selection."""
import codecs
from dataclasses import dataclass
import hashlib
from pathlib import Path
from pathlib import PurePosixPath
import re

import credential_budget as budget
import credential_scope as scope
from credential_git import git, InspectionError
from scanner_runtime import ScannerError


@dataclass(frozen=True)
class VerifiedObject:
    oid: str
    data: bytes
    sha256: str
    lines: int
    source_bytes: int | None = None


class SelectedTextError(ScannerError):
    """Fixed format failure plus the already Git-hash-verified object identity."""
    def __init__(self, code, oid):
        super().__init__(code)
        self.object_oid = oid.decode('ascii')


def select(root, staged, revision, deadline):
    budget.tick(deadline)
    if staged == (revision is not None):
        raise InspectionError('choose-exactly-one-input-mode')
    root = Path(root).resolve(strict=True)
    actual = git(root, 'rev-parse', '--show-toplevel', deadline=deadline).rstrip(b'\n')
    if Path(actual.decode('utf8', 'surrogateescape')).resolve() != root:
        raise InspectionError('root-must-be-git-toplevel')
    fmt = git(root, 'rev-parse', '--show-object-format', deadline=deadline).strip().decode('ascii')
    if fmt not in ('sha1', 'sha256'):
        raise InspectionError('unsupported-object-format')
    commit = None
    if staged:
        manifest = git(root, 'ls-files', '--stage', '-z', deadline=deadline)
    else:
        commit = git(root, 'rev-parse', '--verify', '--end-of-options', revision + '^{commit}', deadline=deadline).strip().decode('ascii')
        manifest = git(root, 'ls-tree', '-rz', '--full-tree', commit, deadline=deadline)
    if len(manifest) > budget.METADATA_BYTES:
        raise ScannerError('credential-metadata-budget')
    entries, seen = [], set()
    for row in filter(None, manifest.split(b'\0')):
        budget.tick(deadline)
        if len(entries) >= budget.ENTRIES:
            raise ScannerError('selected-entry-budget')
        meta, path = row.split(b'\t', 1)
        mode, middle, last = meta.split()
        oid, kind = (middle, b'commit' if mode == b'160000' else b'blob') if staged else (last, middle)
        if staged and last != b'0':
            raise InspectionError('unmerged-index')
        if path in seen or not re.fullmatch(b'[0-9a-f]{' + str(hashlib.new(fmt).digest_size * 2).encode() + b'}', oid):
            raise InspectionError('invalid-index-or-tree-entry')
        seen.add(path)
        entries.append((path.decode('utf8', 'surrogateescape'), mode, kind, oid))
    return root, fmt, commit, manifest, entries


def read_object(root, fmt, oid, size, deadline):
    budget.tick(deadline)
    if size > budget.SINGLE_BYTES:
        raise ScannerError('selected-text-over-16MiB')
    raw = git(root, 'cat-file', 'blob', oid.decode(), deadline=deadline)
    original = hashlib.new(fmt, b'blob ' + str(len(raw)).encode() + b'\0')
    for chunk in budget.chunks(raw, deadline):
        original.update(chunk)
    if len(raw) != size or original.hexdigest() != oid.decode():
        raise ScannerError('selected-object-size-or-hash-mismatch')
    budget.tick(deadline)
    data = raw[3:] if raw.startswith(b'\xef\xbb\xbf') else raw
    decoder, normalized, lines = codecs.getincrementaldecoder('utf8')(), hashlib.sha256(), 1
    for chunk in budget.chunks(data, deadline):
        if 0 in chunk:
            raise SelectedTextError('nul-in-selected-text', oid)
        try:
            decoder.decode(chunk, final=False)
        except UnicodeError:
            raise SelectedTextError('selected-text-not-UTF8', oid) from None
        normalized.update(chunk)
        lines += bytes(chunk).count(b'\n')
    try:
        decoder.decode(b'', final=True)
    except UnicodeError:
        raise SelectedTextError('selected-text-not-UTF8', oid) from None
    budget.tick(deadline)
    return VerifiedObject(oid.decode(), data, normalized.hexdigest(), lines, len(raw))


def fetch_objects(root, fmt, identifiers, deadline):
    budget.tick(deadline)
    if not identifiers:
        return {}
    ids = sorted(identifiers)
    rows = git(root, 'cat-file', '--batch-check', data=b'\n'.join(ids) + b'\n', deadline=deadline).splitlines()
    if len(rows) != len(ids):
        raise ScannerError('incomplete-object-metadata')
    total, sizes = 0, {}
    for oid, row in zip(ids, rows):
        budget.tick(deadline)
        fields = row.split()
        if len(fields) != 3 or fields[:2] != [oid, b'blob'] or not fields[2].isdigit():
            raise ScannerError('unreadable-selected-object')
        sizes[oid] = int(fields[2])
        total += sizes[oid]
        if sizes[oid] > budget.SINGLE_BYTES:
            raise ScannerError('selected-text-over-16MiB')
        if total > budget.UNIQUE_BYTES:
            raise ScannerError('selected-unique-byte-budget')
    result = {}
    for oid in ids:
        budget.tick(deadline)
        result[oid.decode()] = read_object(root, fmt, oid, sizes[oid], deadline)
    return result


def recheck(root, staged, manifest, deadline, commit=None):
    budget.tick(deadline)
    if not staged and not commit:
        raise ScannerError('commit-changed-during-scanner')
    args = ('ls-files', '--stage', '-z') if staged else ('ls-tree', '-rz', '--full-tree', commit)
    if git(root, *args, deadline=deadline) != manifest:
        raise ScannerError('index-changed-during-scanner' if staged else 'commit-changed-during-scanner')
    budget.tick(deadline)


def read_credentials(root, *, staged=False, revision=None, deadline, diagnostic_inherited_jks=False):
    root, fmt, commit, manifest, entries = select(root, staged, revision, deadline)
    aliases, issues, ids, visuals = [], [], set(), set()
    counts = dict(total_entries=len(entries), text_aliases=0, presence_entries=0,
                  denied_entries=0, excluded_entries=0, external_gitlinks=0,
                  known_binary_entries=0, known_binary_objects=0,
                  excluded_by_suffix={}, supported_by_applicability={},
                  evaluated_text_aliases=0, evaluated_unique_objects=0)
    for path, mode, kind, oid in entries:
        budget.tick(deadline)
        digest = hashlib.sha256(path.encode('utf8', 'surrogateescape')).hexdigest()
        try:
            visual = scope.known_binary(path, mode, kind, oid)
            if visual and path in visuals:
                raise ScannerError('known-binary-binding-mismatch')
        except ScannerError:
            counts['denied_entries'] += 1
            issues.append({'code': 'known-binary-binding-mismatch', 'oid': oid.decode(),
                           'path_sha256': digest, 'location': 'manifest-only-no-content-location'})
            continue
        if visual:
            visuals.add(path)
            counts['known_binary_entries'] += 1
            counts['known_binary_objects'] += 1
            continue
        if mode == b'160000' and kind == b'commit':
            counts['external_gitlinks'] += 1
            continue
        category, rules = scope.classify(path)
        if category == 'excluded':
            counts['excluded_entries'] += 1
            suffix = PurePosixPath(path).suffix.lower().lstrip('.')
            counts['excluded_by_suffix'][suffix] = counts['excluded_by_suffix'].get(suffix, 0) + 1
            continue
        if category in ('presence', 'denied') or mode not in (b'100644', b'100755') or kind != b'blob':
            counts['presence_entries' if category == 'presence' else 'denied_entries'] += 1
            issues.append({'code': 'credential-path-presence' if category == 'presence' else 'unsupported-relevant-input',
                           'oid': oid.decode(), 'path_sha256': digest,
                           'rule_sha256': hashlib.sha256(b'pkcs12-file').hexdigest() if category == 'presence' else None,
                           'location': 'manifest-only-no-content-location'})
            continue
        counts['text_aliases'] += 1
        label = rules[0] if rules else 'generic'
        counts['supported_by_applicability'][label] = counts['supported_by_applicability'].get(label, 0) + 1
        ids.add(oid)
        aliases.append({'oid': oid.decode(), 'rules': rules, 'path_sha256': digest})
    # Manifest-only denials never require opening their binary objects.
    objects = {} if issues or diagnostic_inherited_jks else fetch_objects(root, fmt, ids, deadline)
    if not diagnostic_inherited_jks:
        recheck(root, staged, manifest, deadline, commit)
    if not aliases and not issues:
        issues.append({'code': 'zero-credential-text-selected'})
    result = dict(root=root, staged=staged, manifest=manifest, commit=commit, counts=counts,
                selection_sha256=hashlib.sha256(manifest).hexdigest(), objects=objects,
                aliases=aliases, issues=issues)
    if diagnostic_inherited_jks:
        result.update(entries=entries, object_format=fmt)
    return result
