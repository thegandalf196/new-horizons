#!/usr/bin/env python3
"""Fail closed on home/profile paths anywhere in shipped binaries, not just debug.

Reports offsets/categories without echoing personal path contents. Hosted workspace
paths such as D:/a/project are not home directories. A clean scan is not a general
secret audit or proof of source/license completeness.
"""
import argparse
import json
from pathlib import Path
import re

PATTERNS = {
    'unix-home': re.compile(r'/(?:home|Users)/[^/\\\x00\r\n]{1,128}/'),
    'windows-profile': re.compile(r'[A-Za-z]:[\\/]Users[\\/][^\\/\x00\r\n]{1,128}[\\/]', re.IGNORECASE),
}


def inspect_bytes(data):
    hits = []
    views = [('ascii', data.decode('latin-1'), 1, 0)]
    for offset in (0, 1):
        payload = data[offset:offset + ((len(data) - offset) // 2) * 2]
        views.append(('utf16le', payload.decode('utf-16-le', errors='surrogatepass'), 2, offset))
    for encoding, text, width, base in views:
        for category, pattern in PATTERNS.items():
            for match in pattern.finditer(text):
                hits.append({'encoding': encoding, 'category': category,
                             'offset': base + (match.start() if width == 1 else len(text[:match.start()].encode('utf-16-le', errors='surrogatepass')))})
    return sorted(hits, key=lambda hit: (hit['offset'], hit['category']))


def audit_directory(directory):
    report = {}
    for path in sorted(directory.rglob('*')):
        if path.is_file() and path.suffix.lower() in {'.exe', '.dll'}:
            hits = inspect_bytes(path.read_bytes())
            if hits:
                report[path.relative_to(directory).as_posix()] = hits
    return report


def require_clean(directory):
    report = audit_directory(directory)
    if report:
        raise RuntimeError('Personal home/profile paths in shipped binaries (do not publish): ' + ', '.join(report))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    report = audit_directory(args.directory)
    print(json.dumps({'findings': report, 'pass': not report}, indent=2))
    raise SystemExit(1 if report else 0)


if __name__ == '__main__':
    main()
