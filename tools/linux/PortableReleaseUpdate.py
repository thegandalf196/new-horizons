#!/usr/bin/env python3
"""Prepare a verified Linux player release without activating or replacing an install."""
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import ctypes
from datetime import datetime
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import stat
import sys
import tarfile
import tempfile
import time
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener, getproxies

import PortableLauncherLog as launcher_log


API_ROOT = 'https://api.github.com/repos/thegandalf196/new-horizons/releases'
LATEST_URL = API_ROOT + '/latest'
# Beta releases are ordinary GitHub prereleases; their track lists recent releases
# and only ever offers a prerelease, never a stable release.
LIST_URL = API_ROOT + '?per_page=30'
CHANNELS = ('stable', 'beta')
RELEASE_ROOT = 'https://github.com/thegandalf196/new-horizons/releases/'
PACKAGE_ROOT = 'New-Horizons-Linux-x64'
MAX_ARCHIVE_BYTES = 2 * 1024 ** 3
MAX_UNPACKED_BYTES = 4 * 1024 ** 3
MAX_FILES = 20000
MAX_JSON_BYTES = 1024 ** 2
MAX_MANIFEST_BYTES = 8 * 1024 ** 2
CHUNK_BYTES = 1024 ** 2
ASSET_NAME = re.compile(r'New-Horizons-Linux-x64-([0-9a-f]{12})\.tar\.gz\Z')
# Stable releases are MAJOR.MINOR.PATCH; Beta-track prereleases add -beta.N or -rc.N.
VERSION = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-(beta|rc)\.([1-9][0-9]{0,8}))?\Z')
PRERELEASES = {'beta': (0, 'Beta'), 'rc': (1, 'Release Candidate')}  # (precedence, player-facing name)
SHA256 = re.compile(r'[0-9a-f]{64}\Z')
DOWNLOAD_HOSTS = frozenset(('github.com', 'objects.githubusercontent.com', 'release-assets.githubusercontent.com'))
PLAYER_EXECUTABLES = ('Play-New-Horizons.sh', 'new-horizons', 'new-horizons-setup', 'new-horizons-launch.sh')
PLAYER_FILES = (*PLAYER_EXECUTABLES, 'PortableReleaseLauncher.py', 'PortableReleaseUpdate.py',
                'PortableLauncherWindow.py', 'PortableLauncherProcess.py',
                'linux_assets_setup.py', 'libvcmi.so', 'config/filesystem.json',
                'config/newHorizonsCombat.json', 'config/newHorizonsMagic.json', 'config/newHorizonsVersion.json',
                'Mods/vcmi/mod.json', 'Mods/new-horizons/mod.json', 'Mods/new-horizons/NewHorizons.nhart',
                'BUILD-IDENTITY.json', 'new-horizons.features')


def version_key(text):
    """Semantic-version precedence of a release version, or None for anything else:
    0.94.0-beta.9 < 0.94.0-beta.10 < 0.94.0-rc.1 < 0.94.0-rc.2 < 0.94.0."""
    found = VERSION.match(text) if isinstance(text, str) and len(text) <= 32 else None
    if found is None:
        return None
    major, minor, patch, kind, number = found.groups()
    rank = (PRERELEASES[kind][0], int(number)) if kind else (2, 0)
    return (int(major), int(minor), int(patch), *rank)


def is_beta_version(text):
    """Whether a version belongs on the Beta track: a -beta.N or -rc.N prerelease."""
    key = version_key(text)
    return key is not None and key[3] < 2


def version_label(text):
    """The player-facing name: 0.94.0-beta.3 is 0.94.0 Beta 3, 0.94.0-rc.1 is 0.94.0 Release Candidate 1."""
    found = VERSION.match(text) if version_key(text) else None
    if found is None or not found.group(4):
        return text
    return f'{text.split("-", 1)[0]} {PRERELEASES[found.group(4)][1]} {found.group(5)}'


def release_version(release):
    """The version of a release-shaped object tagged vVERSION, or None."""
    tag = release.get('tag_name') if isinstance(release, dict) else None
    return tag[1:] if isinstance(tag, str) and tag.startswith('v') and version_key(tag[1:]) else None


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON field')
        result[key] = value
    return result


def invalid_number(value):
    raise ValueError('Invalid JSON number')


def decode_json(data):
    return json.loads(data.decode('utf-8'), object_pairs_hook=unique_object,
                      parse_constant=invalid_number)


def checked_url(url, hosts):
    parsed = urlsplit(url)
    require(parsed.scheme == 'https' and parsed.hostname in hosts and parsed.port in (None, 443)
            and not parsed.username and not parsed.password and not parsed.fragment,
            'Untrusted HTTPS release location')
    return url


class ReleaseRedirects(HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        checked_url(new_url, DOWNLOAD_HOSTS)
        return super().redirect_request(request, response, code, message, headers, new_url)


def tag_url(version):
    require(version_key(version) is not None, 'Invalid release version')
    return API_ROOT + '/tags/v' + version


def open_https(url, opener, timeout=30):
    api = url == LIST_URL or url == LATEST_URL or url.startswith(API_ROOT + '/tags/v')
    hosts = frozenset(('api.github.com',)) if api else DOWNLOAD_HOSTS
    checked_url(url, hosts)
    # Host and path only: download queries carry signed access tokens. Only
    # whether a proxy is configured is logged, never its address.
    parsed, started = urlsplit(url), time.monotonic()
    launcher_log.write(f'HTTP GET {parsed.hostname}{parsed.path} (timeout {timeout} s, '
                       f'proxy {"yes" if getproxies().get("https") else "no"})')
    try:
        response = opener(Request(url, headers={'User-Agent': 'New-Horizons-Portable-Updater',
                                              'Accept': 'application/vnd.github+json' if api else 'application/octet-stream',
                                              'Cache-Control': 'no-cache'}), timeout=timeout)
    except Exception as error:
        launcher_log.write(f'HTTP request to {parsed.hostname}{parsed.path} failed after '
                           f'{int((time.monotonic() - started) * 1000)} ms: {launcher_log.describe_error(error)}')
        raise
    launcher_log.write(f'HTTP {response.status} after {int((time.monotonic() - started) * 1000)} ms, '
                       f'length {response.headers.get("Content-Length", "unknown")}')
    try:
        require(response.status == 200, 'Release request failed')
        checked_url(response.geturl(), hosts)
    except BaseException:
        response.close()
        raise
    return response


def request_data(path):
    require(path.is_absolute(), 'Update request must use an absolute path')
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, 'rb') as stream:
        info = os.fstat(stream.fileno())
        require(stat.S_ISREG(info.st_mode) and info.st_uid == os.geteuid()
                and not info.st_mode & 0o022 and info.st_size <= MAX_JSON_BYTES,
                'Update request is not a private regular file')
        request = decode_json(stream.read(MAX_JSON_BYTES + 1))
    fields = {'format', 'version', 'name', 'url', 'bytes', 'sha256'}
    require(isinstance(request, dict) and set(request) in (fields, fields | {'channel'}),
            'Invalid update request fields')
    # Format 1 requests predate tracks and always name the stable release.
    require(type(request['format']) is int and request['format'] == (2 if 'channel' in request else 1),
            'Unsupported update request')
    request.setdefault('channel', 'stable')
    require(request['channel'] in CHANNELS, 'Unknown release track')
    require(version_key(request['version']) is not None, 'Invalid release version')
    # The Beta track takes -beta.N and -rc.N versions; a stable release has no suffix.
    require(is_beta_version(request['version']) == (request['channel'] == 'beta'), 'Release version differs from its track')
    require(isinstance(request['name'], str) and ASSET_NAME.fullmatch(request['name']), 'Invalid Linux release name')
    require(type(request['bytes']) is int and 0 < request['bytes'] <= MAX_ARCHIVE_BYTES, 'Invalid release size')
    require(isinstance(request['sha256'], str) and SHA256.fullmatch(request['sha256']), 'Invalid release digest')
    expected = RELEASE_ROOT + 'download/v' + request['version'] + '/' + request['name']
    require(request['url'] == expected, 'Unexpected release download URL')
    return request


def release_metadata(url, opener, timeout=30):
    with open_https(url, opener, timeout=timeout) as response:
        payload = response.read(MAX_JSON_BYTES + 1)
    require(len(payload) <= MAX_JSON_BYTES, 'Release metadata exceeds limit')
    return decode_json(payload)


def latest_release(opener, timeout=30):
    return release_metadata(LATEST_URL, opener, timeout)


def recent_releases(opener, timeout=30):
    releases = release_metadata(LIST_URL, opener, timeout)
    require(isinstance(releases, list) and len(releases) <= 100, 'Invalid release list')
    return releases


def validate_release_metadata(request, release):
    beta = request.get('channel', 'stable') == 'beta'
    require(isinstance(release, dict) and release.get('draft') is False
            and release.get('prerelease') is beta,
            'Expected a published stable release' if not beta else 'Expected a published beta')
    tag = 'v' + request['version']
    require(release.get('tag_name') == tag and release.get('html_url') == RELEASE_ROOT + 'tag/' + tag,
            'Selected release is no longer the official latest version')
    published = release.get('published_at')
    require(isinstance(published, str) and re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z', published),
            'Release publication timestamp is missing')
    datetime.strptime(published, '%Y-%m-%dT%H:%M:%SZ')
    assets = release.get('assets')
    require(isinstance(assets, list) and len(assets) <= 128 and all(isinstance(asset, dict) for asset in assets),
            'Invalid release assets')
    matching = [asset for asset in assets if asset.get('name') == request['name']]
    require(len(matching) == 1, 'Missing or ambiguous Linux release asset')
    asset = matching[0]
    require(asset.get('state') == 'uploaded' and asset.get('browser_download_url') == request['url']
            and type(asset.get('size')) is int and asset['size'] == request['bytes']
            and asset.get('digest') == 'sha256:' + request['sha256'], 'Release asset differs from the selected update')


def revalidate_release(request, opener):
    # Stable updates must still be the official latest release; a beta must
    # still be published under its own tag.
    url = tag_url(request['version']) if request['channel'] == 'beta' else LATEST_URL
    validate_release_metadata(request, release_metadata(url, opener))


def private_directory(path):
    require(path.is_absolute(), 'Update store must use an absolute path')
    require(not any(parent.is_symlink() for parent in (path, *path.parents)), 'Update store cannot contain a link')
    path.mkdir(mode=0o700, exist_ok=True)
    info = path.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid == os.geteuid() and not info.st_mode & 0o077,
            'Update store must be an owned private directory')


def download_archive(request, destination, opener, progress=None):
    digest, received = hashlib.sha256(), 0
    with open_https(request['url'], opener) as response, destination.open('xb') as output:
        length = response.headers.get('Content-Length')
        require(length is None or length.isdigit() and int(length) == request['bytes'], 'Unexpected download length')
        while True:
            chunk = response.read(min(CHUNK_BYTES, request['bytes'] - received + 1))
            if not chunk:
                break
            received += len(chunk)
            require(received <= request['bytes'], 'Download exceeds declared size')
            digest.update(chunk)
            output.write(chunk)
            if progress:
                progress({'phase': 'download', 'received': received, 'total': request['bytes']})
        output.flush()
        os.fsync(output.fileno())
    require(received == request['bytes'] and digest.hexdigest() == request['sha256'], 'Downloaded archive failed integrity verification')


class BoundedTarInfo(tarfile.TarInfo):
    def count_metadata(self, archive):
        total = getattr(archive, '_release_metadata_bytes', 0) + self.size
        count = getattr(archive, '_release_metadata_count', 0) + 1
        require(0 <= self.size <= MAX_JSON_BYTES and total <= MAX_MANIFEST_BYTES and count <= MAX_FILES,
                'Tar metadata exceeds limit')
        archive._release_metadata_bytes = total
        archive._release_metadata_count = count

    def _proc_pax(self, archive):
        self.count_metadata(archive)
        depth = getattr(archive, '_release_metadata_depth', 0)
        require(depth < 32, 'Nested tar metadata exceeds limit')
        archive._release_metadata_depth = depth + 1
        try:
            return super()._proc_pax(archive)
        finally:
            archive._release_metadata_depth = depth

    def _proc_gnulong(self, archive):
        self.count_metadata(archive)
        depth = getattr(archive, '_release_metadata_depth', 0)
        require(depth < 32, 'Nested tar metadata exceeds limit')
        archive._release_metadata_depth = depth + 1
        try:
            return super()._proc_gnulong(archive)
        finally:
            archive._release_metadata_depth = depth

    def _proc_sparse(self, archive):
        raise ValueError('Sparse package members are forbidden')


def canonical_path(name):
    require(isinstance(name, str) and name and '\\' not in name and '\x00' not in name,
            'Invalid package member path')
    path = PurePosixPath(name)
    require(not path.is_absolute() and '..' not in path.parts and str(path) == name
            and path.parts[0] == PACKAGE_ROOT, 'Package member escapes its canonical root')
    return path


class BoundedUnpackedReader:
    def __init__(self, stream):
        self.stream, self.received = stream, 0

    def read(self, size):
        limit = MAX_UNPACKED_BYTES + MAX_MANIFEST_BYTES + MAX_FILES * 1024
        data = self.stream.read(min(size, limit - self.received + 1))
        self.received += len(data)
        require(self.received <= limit, 'Uncompressed archive exceeds limit')
        return data


def extract_archive(archive, staging):
    with gzip.open(archive, 'rb') as compressed:
        reader = BoundedUnpackedReader(compressed)
        with tarfile.open(fileobj=reader, mode='r|', tarinfo=BoundedTarInfo) as package:
            return extract_members(package, reader, staging)


def extract_members(package, reader, staging):
    seen, inventory, total = set(), {}, 0
    for member in package:
        path = canonical_path(member.name)
        require(member.name not in seen and len(seen) < MAX_FILES, 'Duplicate or excessive package members')
        seen.add(member.name)
        require(member.type in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.DIRTYPE)
                and not member.sparse, 'Package links and special files are forbidden')
        require(0 <= member.size <= MAX_ARCHIVE_BYTES, 'Package member exceeds limit')
        target = staging.joinpath(*path.parts)
        if member.isdir():
            require(member.size == 0, 'Directory has a payload')
            target.mkdir(mode=0o700, parents=True, exist_ok=True)
            continue
        require(path != PurePosixPath(PACKAGE_ROOT), 'Package root is not a directory')
        total += member.size
        require(total <= MAX_UNPACKED_BYTES, 'Unpacked release exceeds limit')
        target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        digest = hashlib.sha256()
        with package.extractfile(member) as source, target.open('xb') as output:
            remaining = member.size
            while remaining:
                data = source.read(min(CHUNK_BYTES, remaining))
                require(bool(data), 'Truncated package member')
                output.write(data)
                digest.update(data)
                remaining -= len(data)
        target.chmod(0o600 | (member.mode & 0o111))
        inventory[PurePosixPath(*path.parts[1:]).as_posix()] = digest.hexdigest()
    # Consume the gzip trailer even after tar's end marker, checking CRC and EOF.
    while reader.read(CHUNK_BYTES):
        pass
    return inventory


def verify_payload(root, inventory, request):
    manifest = root / 'SHA256SUMS'
    require(manifest.is_file() and manifest.stat().st_size <= MAX_MANIFEST_BYTES, 'Missing or oversized package checksums')
    expected = {}
    for line in manifest.read_text(encoding='utf-8').splitlines():
        require(len(line) > 66 and line[64:66] == '  ' and SHA256.fullmatch(line[:64]), 'Invalid package checksum record')
        relative = line[66:]
        canonical_path(PACKAGE_ROOT + '/' + relative)
        require(relative not in expected and relative != 'SHA256SUMS', 'Duplicate or self-referential checksum record')
        expected[relative] = line[:64]
    require(set(expected) == set(inventory) - {'SHA256SUMS'} and expected
            and all(inventory[name] == value for name, value in expected.items()), 'Package checksum inventory differs')
    for name in ('config/newHorizonsVersion.json', 'BUILD-IDENTITY.json'):
        path = root / name
        require(path.is_file() and path.stat().st_size <= MAX_JSON_BYTES, 'Missing or oversized package identity')
    version = decode_json((root / 'config/newHorizonsVersion.json').read_bytes())
    identity = decode_json((root / 'BUILD-IDENTITY.json').read_bytes())
    require(isinstance(version, dict) and version.get('version') == request['version'], 'Package product version differs')
    source = identity.get('source_commit') if isinstance(identity, dict) else None
    require(isinstance(source, str) and re.fullmatch(r'[0-9a-f]{40}', source)
            and source.startswith(ASSET_NAME.fullmatch(request['name']).group(1)), 'Package source identity differs')
    verify_player_files(root)
    feature = b'NH_CLIENT_FEATURES_V1\nBUG_REPORT_V1_SHA256=' + inventory['new-horizons'].encode('ascii') + b'\n'
    with (root / 'new-horizons.features').open('rb') as stream:
        require(stream.read(len(feature) + 1) == feature, 'Package client feature declaration differs')


def verify_player_files(root):
    for name in ('config', 'Mods', 'Mods/vcmi', 'Mods/new-horizons'):
        require(stat.S_ISDIR((root / name).lstat().st_mode), f'Invalid packaged player directory: {name}')
    for name in PLAYER_FILES:
        require(stat.S_ISREG((root / name).lstat().st_mode), f'Invalid packaged player file: {name}')
    for name in PLAYER_EXECUTABLES:
        require(os.access(root / name, os.X_OK), f'Package entry point is not executable: {name}')
    with (root / 'Mods/new-horizons/NewHorizons.nhart').open('rb') as stream:
        require(stream.read(5) == b'NHART', 'Invalid packaged artwork header')


def prepare_update(store, request_path, opener=None, progress=None):
    request = request_data(request_path)
    opener = opener or build_opener(ReleaseRedirects()).open
    if progress:
        progress({'phase': 'check'})
    revalidate_release(request, opener)
    private_directory(store)
    releases, staging_root = store / 'releases', store / 'staging'
    private_directory(releases)
    private_directory(staging_root)
    final = releases / request['sha256']
    staging = Path(tempfile.mkdtemp(prefix='prepare-', dir=staging_root))
    try:
        archive = staging / 'release.tar.gz'
        download_archive(request, archive, opener, progress)
        if progress:
            progress({'phase': 'extract'})
        payload = staging / 'payload'
        payload.mkdir(mode=0o700)
        inventory = extract_archive(archive, payload)
        if progress:
            progress({'phase': 'verify'})
        verify_payload(payload / PACKAGE_ROOT, inventory, request)
        if final.exists() or final.is_symlink():
            require(release_tree(final) == release_tree(payload), 'Existing staged release differs from the verified archive')
        else:
            rename_new_directory(payload, final)
        return {'format': 1, 'version': request['version'], 'sha256': request['sha256'],
                'ready': str(final / PACKAGE_ROOT)}
    finally:
        shutil.rmtree(staging)


def release_tree(root):
    """Compare every existing entry with fresh archive bytes without following links."""
    info = root.lstat()
    require(stat.S_ISDIR(info.st_mode), 'Staged release root is not a plain directory')
    inventory = {'': ('directory', stat.S_IMODE(info.st_mode))}
    pending, total = [root], 0
    while pending:
        directory = pending.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                path = Path(entry.path)
                info = entry.stat(follow_symlinks=False)
                name = path.relative_to(root).as_posix()
                require(len(inventory) < MAX_FILES * 4, 'Existing release has excessive entries')
                if stat.S_ISDIR(info.st_mode):
                    inventory[name] = ('directory', stat.S_IMODE(info.st_mode))
                    pending.append(path)
                    continue
                require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1,
                        'Existing release contains a link or special file')
                total += info.st_size
                require(0 <= info.st_size <= MAX_ARCHIVE_BYTES and total <= MAX_UNPACKED_BYTES,
                        'Existing release exceeds size limits')
                descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
                with os.fdopen(descriptor, 'rb') as stream:
                    opened = os.fstat(stream.fileno())
                    require((opened.st_dev, opened.st_ino, opened.st_mode, opened.st_size, opened.st_nlink)
                            == (info.st_dev, info.st_ino, info.st_mode, info.st_size, info.st_nlink),
                            'Existing release entry changed during inspection')
                    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
                inventory[name] = ('file', stat.S_IMODE(info.st_mode), info.st_size, digest)
    return inventory


def rename_new_directory(source, destination):
    """Linux's atomic no-replace rename prevents racing workers overwriting a ready release."""
    libc = ctypes.CDLL(None, use_errno=True)
    require(hasattr(libc, 'renameat2'), 'Atomic release staging is unavailable on this system')
    rename = libc.renameat2
    rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(source), -100, os.fsencode(destination), 1) != 0:
        number = ctypes.get_errno()
        raise OSError(number, os.strerror(number))


def interrupted(signum, frame):
    raise SystemExit(128 + signum)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--store', type=Path, required=True)
    parser.add_argument('--request', type=Path, required=True)
    parser.add_argument('--progress', action='store_true', help='Emit JSON progress on stderr')
    args = parser.parse_args(argv)
    previous = signal.signal(signal.SIGTERM, interrupted)
    launcher_log.join_session('install')
    try:
        progress = (lambda value: print(json.dumps(value), file=sys.stderr, flush=True)) if args.progress else None
        result = prepare_update(args.store, args.request, progress=progress) if progress else prepare_update(args.store, args.request)
        print(json.dumps(result, separators=(',', ':')))
        launcher_log.write('Release ' + str(result.get('version')) + ' prepared and verified')
        return 0
    except (OSError, ValueError, EOFError, tarfile.TarError) as error:
        launcher_log.write('Release preparation failed: ' + launcher_log.describe_error(error))
        print('Release preparation failed: ' + str(error), file=sys.stderr)
        return 1
    finally:
        signal.signal(signal.SIGTERM, previous)


if __name__ == '__main__':
    raise SystemExit(main())
