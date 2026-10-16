# SPDX-License-Identifier: GPL-2.0-or-later
"""Safe path representatives with per-representation completion and work bounds."""
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile

import credential_budget as budget
import credential_scope as scope
from scanner_runtime import ScannerError, command, run


def plan(objects, aliases, deadline):
    """Metadata dedup precedes any data traversal; objects already verified once."""
    result, total = {}, 0
    for index, alias in enumerate(aliases):
        budget.tick(deadline)
        if index >= budget.ENTRIES:
            raise ScannerError('selected-entry-budget')
        key = alias['oid'], alias['rules']
        if key in result:
            continue
        obj = objects[key[0]]
        if not re.fullmatch('[a-f0-9]{40}|[a-f0-9]{64}', obj.oid):
            raise ScannerError('payload-object-identity')
        name = scope.representative(obj.oid, key[1])
        total += len(obj.data)
        if total > budget.PHYSICAL_BYTES or len(result) >= budget.ENTRIES:
            raise ScannerError('credential-physical-budget')
        result[key] = dict(name=name, obj=obj)
    budget.tick(deadline)
    return result


def materialize(directory, planned, deadline):
    expected = {}
    for key, row in planned.items():
        budget.tick(deadline)
        obj, path = row['obj'], directory / row['name']
        with path.open('xb') as output:
            for chunk in budget.chunks(obj.data, deadline):
                output.write(chunk)
        path.chmod(0o400)
        expected[str(path)] = dict(key=key, bytes=len(obj.data), sha256=obj.sha256, lines=obj.lines)
    budget.tick(deadline)
    return expected


def verify_directory(directory, deadline):
    budget.tick(deadline)
    info = directory.lstat()
    if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid()
            or stat.S_IMODE(info.st_mode) != 0o700):
        raise ScannerError('payload-file-type')


def verify(directory, expected, deadline):
    verify_directory(directory, deadline)
    paths = []
    for path in directory.iterdir():
        budget.tick(deadline)
        paths.append(path)
        if len(paths) > budget.ENTRIES:
            raise ScannerError('payload-membership')
    if {str(p) for p in paths} != set(expected):
        raise ScannerError('payload-membership')
    inodes = set()
    for path in paths:
        budget.tick(deadline)
        info, row = path.lstat(), expected[str(path)]
        key = info.st_dev, info.st_ino
        if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or key in inodes
                or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o400):
            raise ScannerError('payload-file-type')
        inodes.add(key)
        if 'inode' in row and row['inode'] != key:
            raise ScannerError('payload-file-type')
        row['inode'] = key
        digest, length = hashlib.sha256(), 0
        with path.open('rb') as source:
            while True:
                budget.tick(deadline)
                chunk = source.read(min(budget.CHUNK, row['bytes'] + 1 - length))
                if not chunk:
                    break
                length += len(chunk)
                digest.update(chunk)
                if length > row['bytes']:
                    break
        if length != row['bytes'] or digest.hexdigest() != row['sha256']:
            raise ScannerError('payload-hash')
    budget.tick(deadline)


def unique_fields(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ScannerError('scanner-report-fields')
        result[key] = value
    return result


def parse_result(code, output, diagnostic, expected, deadline):
    budget.tick(deadline)
    summaries = re.findall(rb'(?:^|\n)[^\n]*\bscanned ~([0-9]+) bytes\b[^\n]*(?=\n|$)', diagnostic)
    total = 0
    for row in expected.values():
        budget.tick(deadline)
        total += row['bytes']
    if len(summaries) != 1 or int(summaries[0]) != total:
        raise ScannerError('scanner-byte-completion')
    if re.search(rb'\b(?:ERR|FTL)\b|partial scan|failed scan|skipping|issue reading', diagnostic):
        raise ScannerError('scanner-incomplete-diagnostic')
    try:
        findings = json.loads(output, object_pairs_hook=unique_fields)
    except (ValueError, UnicodeError, RecursionError):
        raise ScannerError('scanner-report-format') from None
    budget.tick(deadline)
    if not isinstance(findings, list) or len(findings) > budget.FINDINGS:
        raise ScannerError('scanner-report-count')
    if code not in (0, 42) or (code == 42) != bool(findings):
        raise ScannerError('scanner-exit-report-disagreement')
    result, counts = {}, {}
    for finding in findings:
        budget.tick(deadline)
        if not isinstance(finding, dict) or set(finding) != {'file', 'rule', 'start', 'end'}:
            raise ScannerError('scanner-report-fields')
        path, rule, start, end = (finding[k] for k in ('file', 'rule', 'start', 'end'))
        if not isinstance(path, str) or path not in expected:
            raise ScannerError('scanner-report-file')
        row = expected[path]
        if (not isinstance(rule, str) or not re.fullmatch('[a-z0-9][a-z0-9-]{0,95}', rule)
                or type(start) is not int or type(end) is not int or not 1 <= start <= end <= row['lines']):
            raise ScannerError('scanner-report-value')
        if rule in scope.PREDICATES and rule not in row['key'][1]:
            raise ScannerError('scanner-rule-applicability')
        key = row['key']
        counts[key] = counts.get(key, 0) + 1
        if counts[key] > 4096:
            raise ScannerError('scanner-report-count')
        result.setdefault(key, []).append(dict(rule_sha256=hashlib.sha256(rule.encode()).hexdigest(), start=start, end=end))
    budget.tick(deadline)
    return result


def scan(binary, template, objects, aliases, deadline, *, witness=False):
    planned = plan(objects, aliases, deadline)
    with tempfile.TemporaryDirectory(prefix='nhscanv2-') as temporary:
        parent = Path(temporary)
        payload, cwd = parent / 'payload', parent / 'cwd'
        payload.mkdir(mode=0o700)
        cwd.mkdir(mode=0o700)
        expected = materialize(payload, planned, deadline)
        verify(payload, expected, deadline)
        verify_directory(cwd, deadline)
        if list(cwd.iterdir()):
            raise ScannerError('scanner-cwd-not-empty')
        code, output, diagnostic = run(command(binary, template, cwd, payload, witness=witness),
                                       b'', cwd, deadline, timeout=12 if witness else 240)
        verify(payload, expected, deadline)
        verify_directory(cwd, deadline)
        if list(cwd.iterdir()):
            raise ScannerError('scanner-cwd-not-empty')
        parsed = parse_result(code, output, diagnostic, expected, deadline)
    budget.tick(deadline)
    return parsed, dict(representations=len(planned), physical_bytes=sum(r['bytes'] for r in expected.values()))
