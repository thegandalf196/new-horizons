# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate optional build-produced client features without executing the client."""
import hashlib
from pathlib import Path

FILENAME = 'new-horizons.features'
HEADER = b'NH_CLIENT_FEATURES_V1\nBUG_REPORT_V1_SHA256='


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def feature_payload(client_sha256):
    if (not isinstance(client_sha256, str) or len(client_sha256) != 64
            or any(char not in '0123456789abcdef' for char in client_sha256)):
        raise RuntimeError('Invalid client feature identity')
    return HEADER + client_sha256.encode('ascii') + b'\n'


def client_feature_files(client):
    """Missing means legacy; malformed or mismatched declarations are rejected."""
    client = Path(client)
    marker = client.parent / FILENAME
    if not marker.exists() and not marker.is_symlink():
        return {}
    if marker.is_symlink() or not marker.is_file():
        raise RuntimeError('Client features must be a regular non-symlink file')
    with marker.open('rb') as stream:
        payload = stream.read(129)
    if payload != feature_payload(sha256_file(client)):
        raise RuntimeError('Client feature declaration differs from the selected client')
    return {FILENAME: marker}
