#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authenticate a cached scanner archive; never download or execute it here."""
import argparse
import hashlib
import json
from pathlib import Path

from scanner_runtime import ARCHIVE_SHA, ARCHIVE_SIZE, LICENSE_SHA

URL = 'https://github.com/gitleaks/gitleaks/releases/download/v8.30.1/gitleaks_8.30.1_linux_x64.tar.gz'


def lock():
    value = json.loads(Path(__file__).with_name('gitleaks.lock.json').read_text())
    if (not isinstance(value, dict) or value.get('schema') != 1 or value.get('version') != '8.30.1' or value.get('platform') != 'linux_x64'
            or value.get('url') != URL or value.get('bytes') != ARCHIVE_SIZE or value.get('sha256') != ARCHIVE_SHA
            or value.get('license') != 'MIT' or value.get('license_sha256') != LICENSE_SHA
            or value.get('binary_sha256_observed') != '88f91962aa2f93ac6ab281d553b9e125f5197bbbce38f9f2437f7299c32e5509'):
        raise ValueError('scanner-lock-identity')
    return value


def verify(path):
    pinned = lock()
    if path.is_symlink() or not path.is_file():
        raise ValueError('scanner-archive-unavailable')
    with path.open('rb') as source:
        raw = source.read(ARCHIVE_SIZE + 1)
    if len(raw) != pinned['bytes'] or hashlib.sha256(raw).hexdigest() != pinned['sha256']:
        raise ValueError('scanner-archive-identity')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--url', action='store_true')
    mode.add_argument('--verify', type=Path)
    args = parser.parse_args(argv)
    try:
        if args.url:
            print(lock()['url'])
        else:
            verify(args.verify)
            print('scanner-archive-verified')
        return 0
    except (OSError, ValueError, TypeError):
        print('scanner-archive-verification-failed')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
