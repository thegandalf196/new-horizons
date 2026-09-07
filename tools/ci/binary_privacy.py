#!/usr/bin/env python3
"""Fail closed on home/profile paths anywhere in shipped binaries, not just debug.

Reports offsets/categories without echoing personal path contents. Hosted workspace
paths such as D:/a/project are not home directories. A clean scan is not a general
secret audit or proof of source/license completeness.
"""
import argparse
import json
import hashlib
import os
from pathlib import Path
import re

PATTERNS = {
    'unix-home': re.compile(r'/(?:home|Users)/[^/\\\x00\r\n]{1,128}/'),
    'windows-profile': re.compile(r'[A-Za-z]:[\\/]Users[\\/][^\\/\x00\r\n]{1,128}[\\/]', re.IGNORECASE),
}


def verified_github_ci_provenance():
    """Only the successful pinned-cache verification step supplies this marker."""
    cache = '6772d2e9f0a730329a195edce895a863d9115c4bba43101a9a6df3fefe6cfe43'
    if (os.environ.get('GITHUB_ACTIONS') != 'true'
            or os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted'
            or os.environ.get('RUNNER_OS') != 'Windows'
            or os.environ.get('GITHUB_REPOSITORY') != 'thegandalf196/vcmi'
            or os.environ.get('NH_VERIFIED_CONAN_CACHE_SHA256') != cache
            or not re.fullmatch(r'[0-9]+', os.environ.get('GITHUB_RUN_ID', ''))
            or not re.fullmatch(r'[0-9a-f]{40}', os.environ.get('GITHUB_SHA', ''))):
        return None
    return {'kind': 'github-hosted-build-and-verified-pinned-conan-cache',
            'run_id': os.environ['GITHUB_RUN_ID'], 'source_commit': os.environ['GITHUB_SHA'],
            'cache_sha256': cache}


def ci_conan_reference(text, start):
    if not re.match(r'C:[\\/]Users[\\/]runneradmin[\\/]\.conan2[\\/]p[\\/]', text[start:], re.IGNORECASE):
        return False
    # Whitespace may belong to a Windows path: never truncate before checking
    # traversal. A command string may extend this conservative lexical check.
    tail = text[start:].split('\x00', 1)[0]
    depth = 0
    for part in re.split(r'[\\/]', tail)[5:]:
        if part == '..':
            if depth == 0:
                return False
            depth -= 1
        elif part not in ('', '.'):
            depth += 1
    return True


def inspect_bytes(data, verified_ci=False):
    hits = []
    views = [('ascii', data.decode('latin-1'), 1, 0)]
    for offset in (0, 1):
        payload = data[offset:offset + ((len(data) - offset) // 2) * 2]
        views.append(('utf16le', payload.decode('utf-16-le', errors='surrogatepass'), 2, offset))
    for encoding, text, width, base in views:
        for category, pattern in PATTERNS.items():
            for match in pattern.finditer(text):
                hits.append({'encoding': encoding, 'category': category,
                             'classification': 'ci-service-build-path' if verified_ci and category == 'windows-profile' and ci_conan_reference(text, match.start()) else 'unapproved-profile-path',
                             'offset': base + (match.start() if width == 1 else len(text[:match.start()].encode('utf-16-le', errors='surrogatepass')))})
    return sorted(hits, key=lambda hit: (hit['offset'], hit['category']))


def audit_directory(directory, ci_provenance=None):
    report = {}
    for path in sorted(directory.rglob('*')):
        if path.is_file() and path.suffix.lower() in {'.exe', '.dll'}:
            hits = inspect_bytes(path.read_bytes(), verified_ci=ci_provenance is not None)
            if hits:
                report[path.relative_to(directory).as_posix()] = hits
    return report


def require_clean(directory, ci_provenance=None):
    report = audit_directory(directory, ci_provenance)
    blocked = {name: hits for name, hits in report.items() if any(h['classification'] != 'ci-service-build-path' for h in hits)}
    if ci_provenance is not None:
        checksums = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in report}
        (directory / 'BINARY-PRIVACY.json').write_text(json.dumps({'provenance': ci_provenance,
            'reported_profile_paths': report, 'binary_sha256': checksums, 'pass': not blocked,
            'scope': 'Exact verified CI service cache subtree is reported, not personal workstation data; unknown profiles still fail'}, indent=2) + '\n')
    if blocked:
        raise RuntimeError('Personal home/profile paths in shipped binaries (do not publish): ' + ', '.join(blocked))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    report = audit_directory(args.directory)
    print(json.dumps({'findings': report, 'pass': not report}, indent=2))
    raise SystemExit(1 if report else 0)


if __name__ == '__main__':
    main()
