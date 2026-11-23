"""Offline security and transaction tests for the Linux player release worker."""
# SPDX-License-Identifier: GPL-2.0-or-later

import hashlib
import io
import json
import os
from pathlib import Path
import signal
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/linux'))
import PortableReleaseUpdate as update


def checksum(data):
    return hashlib.sha256(data).hexdigest()


class Response(io.BytesIO):
    def __init__(self, payload, url, status=200, length=None):
        super().__init__(payload)
        self.url, self.status = url, status
        self.headers = {'Content-Length': str(len(payload) if length is None else length)}

    def geturl(self):
        return self.url


class LinuxPortableUpdateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.store, self.request_path = self.root / 'store', self.root / 'request.json'
        self.source = 'abcdef123456' + '0' * 28
        self.files = {
            'Play-New-Horizons.sh': b'#!/bin/sh\nexit 0\n',
            'new-horizons': b'Never executed fixture client',
            'new-horizons-setup': b'Never executed fixture setup',
            'config/newHorizonsVersion.json': b'{"version":"0.93.0"}',
            'BUILD-IDENTITY.json': json.dumps({'source_commit': self.source}).encode(),
        }
        for name in update.PLAYER_FILES:
            self.files.setdefault(name, name.encode())
        self.files['Mods/new-horizons/NewHorizons.nhart'] = b'NHARTfixture'
        self.files['new-horizons.features'] = (b'NH_CLIENT_FEATURES_V1\nBUG_REPORT_V1_SHA256='
                                               + checksum(self.files['new-horizons']).encode() + b'\n')
        self.archive = self.make_archive()
        self.metadata = None
        self.response_url = 'https://release-assets.githubusercontent.com/github-production-release-asset/fixture'
        self.request = self.write_request()
        self.calls = []

    def make_archive(self, extra=(), modify_manifest=None, modes=None):
        files = dict(self.files)
        manifest = ''.join(f'{checksum(data)}  {name}\n' for name, data in sorted(files.items()))
        if modify_manifest:
            manifest = modify_manifest(manifest)
        files['SHA256SUMS'] = manifest.encode()
        output = io.BytesIO()
        with tarfile.open(fileobj=output, mode='w:gz', format=tarfile.PAX_FORMAT) as archive:
            for name, data in files.items():
                item = tarfile.TarInfo(update.PACKAGE_ROOT + '/' + name)
                item.size = len(data)
                item.mode = (modes or {}).get(name, 0o755 if name in update.PLAYER_EXECUTABLES else 0o644)
                archive.addfile(item, io.BytesIO(data))
            for item, data in extra:
                archive.addfile(item, io.BytesIO(data) if data is not None else None)
        return output.getvalue()

    def write_request(self, **changes):
        request = {'format': 1, 'version': '0.93.0',
                   'name': 'New-Horizons-Linux-x64-' + self.source[:12] + '.tar.gz',
                   'bytes': len(self.archive), 'sha256': checksum(self.archive)}
        request['url'] = update.RELEASE_ROOT + 'download/v0.93.0/' + request['name']
        request.update(changes)
        self.request_path.write_text(json.dumps(request))
        self.request_path.chmod(0o600)
        return request

    def latest(self):
        return {'draft': False, 'prerelease': False, 'tag_name': 'v0.93.0',
                'html_url': update.RELEASE_ROOT + 'tag/v0.93.0', 'published_at': '2026-11-21T12:00:00Z',
                'assets': [{'name': self.request['name'], 'state': 'uploaded',
                            'browser_download_url': self.request['url'], 'size': self.request['bytes'],
                            'digest': 'sha256:' + self.request['sha256']}]}

    def transport(self, request, timeout):
        self.calls.append(request.full_url)
        self.assertEqual(timeout, 30)
        if request.full_url == update.LATEST_URL:
            raw = self.metadata if isinstance(self.metadata, bytes) else json.dumps(self.metadata or self.latest()).encode()
            return Response(raw, update.LATEST_URL)
        self.assertEqual(request.full_url, self.request['url'])
        return Response(self.archive, self.response_url)

    def prepare(self):
        return update.prepare_update(self.store, self.request_path, self.transport)

    def rejected(self):
        with self.assertRaises((OSError, ValueError, EOFError, tarfile.TarError)):
            self.prepare()
        self.assertFalse((self.store / 'releases' / self.request['sha256']).exists())
        if (self.store / 'staging').exists():
            self.assertEqual(list((self.store / 'staging').iterdir()), [])

    def test_verified_release_is_staged_without_changing_selector_or_existing_install(self):
        self.store.mkdir(mode=0o700)
        selector = self.store / 'current.json'
        selector.write_text('old selector')
        (self.store / 'releases').mkdir(mode=0o700)
        old = self.store / 'releases/old-release'
        old.mkdir(mode=0o700, parents=True)
        (old / 'original').write_text('unchanged')
        result = self.prepare()
        ready = Path(result['ready'])
        self.assertEqual(result, {'format': 1, 'version': '0.93.0', 'sha256': self.request['sha256'],
                                  'ready': str(self.store / 'releases' / self.request['sha256'] / update.PACKAGE_ROOT)})
        self.assertEqual((ready / 'new-horizons').read_bytes(), self.files['new-horizons'])
        self.assertTrue(os.access(ready / 'Play-New-Horizons.sh', os.X_OK))
        self.assertEqual(selector.read_text(), 'old selector')
        self.assertEqual((old / 'original').read_text(), 'unchanged')
        self.assertEqual(list((self.store / 'staging').iterdir()), [])
        self.assertEqual(self.calls, [update.LATEST_URL, self.request['url']])

    def test_valid_manifest_cannot_omit_required_player_resource(self):
        del self.files['config/filesystem.json']
        self.archive = self.make_archive()
        self.request = self.write_request()
        self.rejected()

    def test_repeated_preparation_revalidates_archive_and_reuses_exact_ready_bytes(self):
        first = self.prepare()
        ready = Path(first['ready'])
        original = {path.relative_to(ready): (path.stat().st_ino, path.stat().st_mode, path.stat().st_mtime_ns,
                                              path.read_bytes() if path.is_file() else None)
                    for path in ready.rglob('*')}
        with patch.object(update, 'rename_new_directory') as rename:
            self.assertEqual(self.prepare(), first)
            rename.assert_not_called()
        current = {path.relative_to(ready): (path.stat().st_ino, path.stat().st_mode, path.stat().st_mtime_ns,
                                             path.read_bytes() if path.is_file() else None)
                   for path in ready.rglob('*')}
        self.assertEqual(current, original)
        self.assertEqual(self.calls, [update.LATEST_URL, self.request['url']] * 2)
        self.assertEqual(list((self.store / 'staging').iterdir()), [])

    def test_progress_reports_actual_download_and_verification_phases(self):
        progress = []
        result = update.prepare_update(self.store, self.request_path, self.transport, progress.append)
        self.assertTrue(Path(result['ready']).is_dir())
        self.assertEqual([row['phase'] for row in progress], ['check', 'download', 'extract', 'verify'])
        self.assertEqual(progress[1]['received'], self.request['bytes'])
        self.assertEqual(progress[1]['total'], self.request['bytes'])

    def test_existing_ready_package_mutation_is_refused_without_repairing_any_bytes(self):
        result = self.prepare()
        final = Path(result['ready']).parent

        def replace_with_link(package):
            target = package / 'new-horizons'
            target.unlink()
            target.symlink_to(self.request_path)

        def make_hard_link(package):
            os.link(package / 'new-horizons', package / 'linked-client')

        def snapshot(path):
            info = path.lstat()
            mode = stat.S_IMODE(info.st_mode)
            if stat.S_ISLNK(info.st_mode):
                return ('link', mode, os.readlink(path))
            if stat.S_ISREG(info.st_mode):
                return ('file', mode, path.read_bytes())
            if stat.S_ISDIR(info.st_mode):
                return ('directory', mode, {child.name: snapshot(child) for child in path.iterdir()})
            return ('special', mode)

        mutations = (
            lambda package: (package / 'new-horizons').write_bytes(b'changed client'),
            lambda package: (package / 'SHA256SUMS').write_text('forged manifest'),
            lambda package: (package / 'Play-New-Horizons.sh').chmod(0o600),
            lambda package: (package / 'extra').write_text('unlisted'),
            lambda package: (package / 'empty-extra-directory').mkdir(),
            replace_with_link,
            make_hard_link,
            lambda package: os.mkfifo(package / 'pipe'),
            lambda package: (package / 'linked-directory').symlink_to(self.root, target_is_directory=True),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                package = final / update.PACKAGE_ROOT
                mutate(package)
                before = snapshot(final)
                with self.assertRaises((OSError, ValueError)):
                    self.prepare()
                self.assertEqual(snapshot(final), before)
                self.assertEqual(list((self.store / 'staging').iterdir()), [])
                shutil.rmtree(final)
                self.prepare()

    def test_ready_directory_does_not_bypass_fresh_official_or_archive_verification(self):
        ready = Path(self.prepare()['ready'])
        before = update.release_tree(ready)
        self.metadata = self.latest()
        self.metadata['draft'] = True
        with self.assertRaises(ValueError):
            self.prepare()
        self.metadata = None
        self.archive = b'wrong downloaded bytes'
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(update.release_tree(ready), before)
        self.assertEqual(list((self.store / 'staging').iterdir()), [])

    def test_outer_digest_and_length_rejected_before_tar_processing(self):
        for change in ({'sha256': '0' * 64}, {'bytes': len(self.archive) - 1}, {'bytes': len(self.archive) + 1}):
            with self.subTest(change=change):
                self.request = self.write_request(**change)
                with patch.object(update, 'extract_archive') as extract:
                    self.rejected()
                    extract.assert_not_called()

    def test_tar_paths_links_duplicates_and_special_files_are_rejected(self):
        for path, kind in (('../outside', tarfile.REGTYPE), ('/absolute', tarfile.REGTYPE),
                           (update.PACKAGE_ROOT + '/a/../escape', tarfile.REGTYPE),
                           (update.PACKAGE_ROOT + '//double', tarfile.REGTYPE),
                           (update.PACKAGE_ROOT + '/back\\slash', tarfile.REGTYPE),
                           ('OtherRoot/file', tarfile.REGTYPE),
                           (update.PACKAGE_ROOT + '/link', tarfile.SYMTYPE),
                           (update.PACKAGE_ROOT + '/hardlink', tarfile.LNKTYPE),
                           (update.PACKAGE_ROOT + '/pipe', tarfile.FIFOTYPE),
                           (update.PACKAGE_ROOT + '/new-horizons', tarfile.REGTYPE)):
            with self.subTest(path=path, kind=kind):
                item = tarfile.TarInfo(path)
                item.type, item.size, item.linkname = kind, 0, '../../outside'
                self.archive = self.make_archive(((item, b''),))
                self.request = self.write_request()
                self.rejected()
                self.assertFalse((self.root / 'outside').exists())

    def test_checksum_inventory_missing_extra_and_duplicate_records_are_rejected(self):
        alterations = (lambda text: '\n'.join(text.splitlines()[1:]) + '\n',
                       lambda text: text + '0' * 64 + '  absent\n',
                       lambda text: text + text.splitlines()[0] + '\n',
                       lambda text: text.replace('Play-New-Horizons.sh', '../outside'),
                       lambda text: text.replace(checksum(self.files['new-horizons']), '0' * 64))
        for index, alteration in enumerate(alterations):
            with self.subTest(index=index):
                self.archive = self.make_archive(modify_manifest=alteration)
                self.request = self.write_request()
                self.rejected()

    def test_version_identity_and_executable_permissions_must_match(self):
        for name, payload in (('config/newHorizonsVersion.json', b'{"version":"0.90.0"}'),
                              ('BUILD-IDENTITY.json', b'{"source_commit":"0000000000000000000000000000000000000000"}')):
            original = self.files[name]
            self.files[name] = payload
            self.archive = self.make_archive()
            self.request = self.write_request()
            self.rejected()
            self.files[name] = original
        self.archive = self.make_archive(modes={'Play-New-Horizons.sh': 0o644})
        self.request = self.write_request()
        self.rejected()

    def test_malformed_or_changed_official_metadata_is_rejected(self):
        for field, value in (('draft', True), ('prerelease', True), ('tag_name', 'v0.94.0'),
                             ('published_at', '2026-02-31T00:00:00Z'), ('html_url', 'https://evil.invalid/release'),
                             ('assets', [])):
            self.metadata = self.latest()
            self.metadata[field] = value
            self.rejected()
        for field, value in (('size', True), ('digest', 'sha256:' + '0' * 64), ('state', 'uploading'),
                             ('browser_download_url', 'https://evil.invalid/payload')):
            self.metadata = self.latest()
            self.metadata['assets'][0][field] = value
            self.rejected()
        for raw in (b'not json', b'{"draft":false,"draft":true}', b'[]', b'x' * (update.MAX_JSON_BYTES + 1)):
            self.metadata = raw
            self.rejected()
        self.metadata = self.latest()
        self.metadata['assets'] *= 2
        self.rejected()

    def test_beta_request_revalidates_its_own_tag_and_accepts_only_a_prerelease(self):
        self.request = self.write_request(format=2, channel='beta')
        self.metadata = dict(self.latest(), prerelease=True)
        tag = update.tag_url('0.93.0')
        def transport(request, timeout):
            if request.full_url == tag:
                self.calls.append(request.full_url)
                return Response(json.dumps(self.metadata).encode(), tag)
            self.assertNotEqual(request.full_url, update.LATEST_URL)
            return self.transport(request, timeout)
        result = update.prepare_update(self.store, self.request_path, transport)
        self.assertEqual((result['version'], self.calls[0]), ('0.93.0', tag))
        self.metadata['draft'] = True
        with self.assertRaises(ValueError):
            update.prepare_update(self.store, self.request_path, transport)
        # A stable release is never installed on the Beta track.
        self.metadata.update(draft=False, prerelease=False)
        with self.assertRaisesRegex(ValueError, 'Expected a published beta'):
            update.prepare_update(self.store, self.request_path, transport)

    def test_track_requests_cannot_mix_formats_name_unknown_tracks_or_take_a_stable_prerelease(self):
        for change in ({'format': 1, 'channel': 'beta'}, {'format': 2}, {'format': 2, 'channel': 'nightly'}):
            with self.subTest(change=change):
                self.request = self.write_request(**change)
                self.rejected()
        self.request = self.write_request(format=2, channel='stable')
        self.metadata = dict(self.latest(), prerelease=True)
        self.rejected()
        with self.assertRaises(ValueError):
            update.tag_url('0.93.0/../latest')

    def test_requests_require_exact_owned_private_file_and_fields(self):
        for change in ({'format': True}, {'version': '01.2.3'}, {'bytes': 2 ** 31 + 1}, {'bytes': -1},
                       {'extra': 'ignored'}, {'sha256': 'F' * 64}, {'url': 'http://github.com/payload'},
                       {'name': 'New-Horizons-Linux-x64-ABCDEFG12345.tar.gz'}):
            with self.subTest(change=change):
                self.request = self.write_request(**change)
                self.rejected()
        self.request = self.write_request()
        self.request_path.chmod(0o666)
        self.rejected()
        self.request_path.chmod(0o600)
        link = self.root / 'request-link.json'
        link.symlink_to(self.request_path)
        with self.assertRaises(OSError):
            update.prepare_update(self.store, link, self.transport)

    def test_redirects_and_response_locations_reject_untrusted_hosts_and_schemes(self):
        for url in ('http://github.com/file', 'https://github.com.evil.invalid/file',
                    'https://user:secret@github.com/file', 'https://github.com:444/file',
                    'https://evil.invalid/file', 'https://release-assets.githubusercontent.com/file#part'):
            with self.subTest(url=url):
                self.response_url = url
                self.rejected()
                with self.assertRaises(ValueError):
                    update.ReleaseRedirects().redirect_request(None, None, 302, '', {}, url)

    def test_truncated_gzip_and_bounded_extraction_fail_cleanly(self):
        intact = self.archive
        self.archive = self.archive[:len(self.archive) // 2]
        self.request = self.write_request()
        self.rejected()
        self.archive = intact[:-8]
        self.request = self.write_request()
        self.rejected()
        self.archive = self.make_archive()
        self.request = self.write_request()
        with patch.object(update, 'MAX_FILES', 2):
            self.rejected()
        with patch.object(update, 'MAX_UNPACKED_BYTES', 1):
            self.rejected()

    def test_interruption_cleans_only_owned_staging_and_preserves_existing_data(self):
        def interrupted_transport(request, timeout):
            response = self.transport(request, timeout)
            if request.full_url != update.LATEST_URL:
                response.read = lambda count: (_ for _ in ()).throw(KeyboardInterrupt())
            return response
        with self.assertRaises(KeyboardInterrupt):
            update.prepare_update(self.store, self.request_path, interrupted_transport)
        self.assertEqual(list((self.store / 'staging').iterdir()), [])
        self.assertEqual(list((self.store / 'releases').iterdir()), [])
        self.assertTrue(self.request_path.exists())

    def test_terminated_worker_process_cleans_download_staging(self):
        metadata = self.root / 'metadata.json'
        metadata.write_text(json.dumps(self.latest()))
        blocked = self.root / 'blocked'
        script = self.root / 'interrupted.py'
        script.write_text('''import io, json, pathlib, sys, time
sys.path.insert(0, sys.argv[1])
import PortableReleaseUpdate as worker
metadata, blocked = map(pathlib.Path, sys.argv[2:4])
class Response(io.BytesIO):
    status = 200
    headers = {}
    def __init__(self, request):
        self.url = request.full_url
        super().__init__(metadata.read_bytes() if self.url == worker.LATEST_URL else b'')
    def geturl(self):
        return self.url
    def read(self, size):
        if self.url != worker.LATEST_URL:
            blocked.touch()
            time.sleep(30)
        return super().read(size)
def opener(request, timeout):
    return Response(request)
original = worker.prepare_update
worker.prepare_update = lambda store, request: original(store, request, opener)
raise SystemExit(worker.main(sys.argv[4:]))
''')
        child = subprocess.Popen([sys.executable, '-B', str(script), str(ROOT / 'tools/linux'),
                                  str(metadata), str(blocked), '--store', str(self.store),
                                  '--request', str(self.request_path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while not blocked.exists() and child.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(blocked.exists())
            child.send_signal(signal.SIGTERM)
            output, errors = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 143, errors.decode())
            self.assertEqual(output, b'')
            self.assertEqual(list((self.store / 'staging').iterdir()), [])
            self.assertEqual(list((self.store / 'releases').iterdir()), [])
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate()

    def test_atomic_publish_refuses_existing_directory_and_store_links(self):
        source, final = self.root / 'source', self.root / 'existing'
        source.mkdir()
        final.mkdir()
        with self.assertRaises(FileExistsError):
            update.rename_new_directory(source, final)
        self.assertTrue(source.is_dir())
        self.assertTrue(final.is_dir())
        actual = self.root / 'elsewhere'
        actual.mkdir(mode=0o700)
        self.store.symlink_to(actual, target_is_directory=True)
        self.rejected()
        self.assertEqual(list(actual.iterdir()), [])

    def test_cli_preserves_sigterm_cleanup_and_only_prints_success_json(self):
        result = {'format': 1, 'version': '0.93.0', 'sha256': 'a' * 64, 'ready': '/fixture/ready'}
        old = signal.getsignal(signal.SIGTERM)
        with patch.object(update, 'prepare_update', return_value=result), patch('sys.stdout', new_callable=io.StringIO) as output:
            self.assertEqual(update.main(['--store', str(self.store), '--request', str(self.request_path)]), 0)
            self.assertEqual(json.loads(output.getvalue()), result)
        self.assertEqual(signal.getsignal(signal.SIGTERM), old)
        with self.assertRaises(SystemExit) as interrupted:
            update.interrupted(signal.SIGTERM, None)
        self.assertEqual(interrupted.exception.code, 143)


if __name__ == '__main__':
    unittest.main()
