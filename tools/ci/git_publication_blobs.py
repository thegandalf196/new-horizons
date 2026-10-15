#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read explicitly selected index/commit text objects, never worktree payloads."""
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import subprocess

MAX_TEXT_BYTES = 16 * 1024 * 1024
SUFFIXES = frozenset('md txt rst json jsonc yaml yml toml csv cpp cc cxx c h hpp hh hxx py lua sh bash ps1 bat cmd cmake mjs js ts html css xml qrc ui sql nsi nsh iss gradle nix ini cfg conf properties qss pro rc in'.split())
NAMES = frozenset(x.lower() for x in ('AGENTS', 'README', 'LICENSE', 'AUTHORS', 'COPYING',
                  'CMakeLists.txt', 'Makefile', 'Dockerfile', '.gitignore', '.gitattributes', '.gitmodules'))


class InspectionError(ValueError):
    """Sanitized inspection failure; never include raw subprocess stderr."""


def git(root, *args, data=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
    env.update(GIT_CONFIG_GLOBAL='/dev/null', GIT_CONFIG_SYSTEM='/dev/null',
               GIT_CONFIG_NOSYSTEM='1', GIT_OPTIONAL_LOCKS='0',
               GIT_NO_LAZY_FETCH='1', GIT_NO_REPLACE_OBJECTS='1', LC_ALL='C')
    try:
        result = subprocess.run(['/usr/bin/git', '--no-lazy-fetch', '--no-replace-objects', '-c', 'core.fsmonitor=false', '-C', str(root), *args],
                                input=data, capture_output=True, env=env, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        raise InspectionError('git-inspection-unavailable') from None
    if result.returncode:
        raise InspectionError('git-inspection-failed')
    return result.stdout


def selected_text(path):
    name = PurePosixPath(path).name
    suffix = PurePosixPath(path).suffix.lower().lstrip('.')
    return (suffix in SUFFIXES or name.lower() in NAMES or path in {'vcmibuilder', 'android/gradlew'}
            or path.startswith('packaging/') and name == 'rules')


def selection(root, staged, revision):
    if staged == (revision is not None):
        raise InspectionError('choose-exactly-one-input-mode')
    try:
        root = Path(root).resolve(strict=True)
    except OSError:
        raise InspectionError('invalid-root') from None
    actual = git(root, 'rev-parse', '--show-toplevel').rstrip(b'\n').decode('utf-8', 'surrogateescape')
    if Path(actual).resolve() != root:
        raise InspectionError('root-must-be-git-toplevel')
    fmt = git(root, 'rev-parse', '--show-object-format').strip().decode('ascii')
    if fmt not in {'sha1', 'sha256'}:
        raise InspectionError('unsupported-object-format')
    if staged:
        manifest = git(root, 'ls-files', '--stage', '-z')
        commit = None
    else:
        commit = git(root, 'rev-parse', '--verify', '--end-of-options', revision + '^{commit}').strip().decode('ascii')
        manifest = git(root, 'ls-tree', '-rz', '--full-tree', commit)
    entries, seen = [], set()
    for row in filter(None, manifest.split(b'\0')):
        meta, path = row.split(b'\t', 1)
        mode, middle, last = meta.split()
        oid, kind = (middle, b'commit' if mode == b'160000' else b'blob') if staged else (last, middle)
        if staged and last != b'0':
            raise InspectionError('unmerged-index')
        if path in seen or not re.fullmatch(b'[0-9a-f]{' + str(hashlib.new(fmt).digest_size * 2).encode() + b'}', oid):
            raise InspectionError('invalid-index-or-tree-entry')
        seen.add(path)
        entries.append((path.decode('utf-8', 'surrogateescape'), mode, kind, oid))
    return root, fmt, commit, manifest, entries


def object_sizes(root, entries):
    identifiers = sorted({oid for path, mode, kind, oid in entries
                          if selected_text(path) and mode in {b'100644', b'100755'} and kind == b'blob'})
    if not identifiers:
        return {}
    output = git(root, 'cat-file', '--batch-check', data=b'\n'.join(identifiers) + b'\n').splitlines()
    if len(output) != len(identifiers):
        raise InspectionError('incomplete-object-metadata')
    sizes = {}
    for expected, row in zip(identifiers, output):
        fields = row.split()
        if len(fields) != 3 or fields[0] != expected or fields[1] != b'blob' or not fields[2].isdigit():
            raise InspectionError('unreadable-selected-object')
        sizes[expected] = int(fields[2])
    return sizes


def read_text(root, fmt, oid, size):
    if size > MAX_TEXT_BYTES:
        raise InspectionError('selected-text-over-16MiB')
    data = git(root, 'cat-file', 'blob', oid.decode('ascii'))
    digest = hashlib.new(fmt, b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if len(data) != size or digest != oid.decode('ascii'):
        raise InspectionError('selected-object-size-or-hash-mismatch')
    if b'\0' in data:
        raise InspectionError('nul-in-selected-text')
    try:
        return data.decode('utf-8-sig')
    except UnicodeError:
        raise InspectionError('selected-text-not-UTF8') from None


def read_publication_blobs(root, *, staged=False, revision=None):
    root, fmt, commit, manifest, entries = selection(root, staged, revision)
    sizes = object_sizes(root, entries)
    counts = {'total_entries': len(entries), 'selected_text_entries': 0, 'scanned_text_entries': 0,
              'out_of_scope_entries': 0, 'external_gitlinks': 0, 'unique_object_reads': 0,
              'largest_selected_blob_bytes': max(sizes.values(), default=0)}
    texts, issues, cache = [], [], {}
    for path, mode, kind, oid in entries:
        if mode == b'160000' and kind == b'commit':
            counts['external_gitlinks'] += 1
            continue
        if not selected_text(path):
            counts['out_of_scope_entries'] += 1
            continue
        counts['selected_text_entries'] += 1
        if mode not in {b'100644', b'100755'} or kind != b'blob':
            issues.append({'path': path, 'code': 'unsupported-selected-text-mode'})
            continue
        if oid not in cache:
            counts['unique_object_reads'] += 1
            try:
                cache[oid] = (read_text(root, fmt, oid, sizes[oid]), None)
            except InspectionError as error:
                cache[oid] = (None, str(error))
        text, error = cache[oid]
        if error:
            issues.append({'path': path, 'code': error})
        else:
            counts['scanned_text_entries'] += 1
            texts.append({'path': path, 'oid': oid.decode('ascii'), 'text': text})
    if staged and git(root, 'ls-files', '--stage', '-z') != manifest:
        raise InspectionError('index-changed-during-inspection')
    if not counts['scanned_text_entries']:
        issues.append({'code': 'zero-selected-text-scanned'})
    return {'mode': 'staged' if staged else 'commit', 'commit': commit, 'counts': counts,
            'issues': issues, 'blobs': texts, 'object_format': fmt,
            'selection_sha256': hashlib.sha256(manifest).hexdigest()}
