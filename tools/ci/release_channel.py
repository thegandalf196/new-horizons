#!/usr/bin/env python3
"""Select the release track a player package records for its launchers.

Launchers open a package on this track; a beta package keeps separate saves.
A package without a track predates it and is stable.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path

CHANNELS = ('stable', 'beta')


def release_channel(value, ref):
    """The dispatched track. Release tags must name one (beta for a GitHub prerelease)."""
    if value in CHANNELS or (value == '' and not ref.startswith('refs/tags/v')):
        return value
    if value == '':
        raise ValueError('A release tag build needs release_channel stable or beta (beta for a GitHub prerelease).')
    raise ValueError('release_channel must be stable, beta or empty.')


def main():
    channel = release_channel(os.environ.get('RELEASE_CHANNEL', ''), os.environ.get('GITHUB_REF', ''))
    with Path(os.environ['GITHUB_ENV']).open('a', encoding='utf-8') as stream:
        stream.write('NH_RELEASE_CHANNEL=' + channel + '\n')


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        print('::error::' + str(error))
        raise SystemExit(1)
