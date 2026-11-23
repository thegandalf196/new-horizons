#!/usr/bin/env python3
"""Select the release track a player package records for its launchers.

Launchers open a package on this track; a beta package keeps separate saves.
A package without a track predates it and is stable.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import sys

CHANNELS = ('stable', 'beta')
TAG_PREFIX = 'refs/tags/v'


def release_channel(value, ref):
    """The dispatched track. A release tag must name one, matching its version:
    -beta.N and -rc.N prereleases play on beta, a plain MAJOR.MINOR.PATCH on stable."""
    if not ref.startswith(TAG_PREFIX):
        if value in CHANNELS or value == '':
            return value
        raise ValueError('release_channel must be stable, beta or empty.')
    if value not in CHANNELS:
        raise ValueError('A release tag build needs release_channel stable or beta (beta for a GitHub prerelease).')
    return require_version_channel(ref[len(TAG_PREFIX):], value, 'the tag ' + ref)


def require_version_channel(version, channel, origin):
    """-beta.N and -rc.N versions are played on the Beta track, plain versions on Stable."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'linux'))
    from PortableReleaseUpdate import is_beta_version, version_key  # The launchers' own version rules.
    if version_key(version) is None:
        raise ValueError(origin + ' is not a New Horizons release version.')
    if is_beta_version(version) != (channel == 'beta'):
        raise ValueError('release_channel ' + channel + ' does not match ' + origin +
                         ': -beta.N and -rc.N versions are beta, plain versions are stable.')
    return channel


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
