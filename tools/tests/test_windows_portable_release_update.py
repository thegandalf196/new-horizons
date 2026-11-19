"""Offline portable updater admission fixtures run through PowerShell when available."""
# SPDX-License-Identifier: GPL-2.0-or-later

import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / 'tools/windows/Prepare-NHReleaseUpdate.ps1'
POWERSHELL = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')


def digest(data):
    return hashlib.sha256(data).hexdigest()


@unittest.skipUnless(POWERSHELL, 'Set NH_TEST_POWERSHELL to run offline PowerShell fixtures')
class WindowsPortableReleaseUpdateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.store = self.root / 'store'
        self.source = self.root / 'fixture.zip'
        self.request = self.root / 'request.json'
        self.metadata = self.root / 'metadata.json'
        self.revision = 'a' * 40
        self.name = 'New-Horizons-Windows-x64-' + self.revision[:12] + '.zip'
        self.offer = {'format': 1, 'version': '0.93.0', 'name': self.name,
                      'url': 'https://github.com/thegandalf196/new-horizons/releases/download/v0.93.0/' + self.name,
                      'bytes': 1, 'sha256': 'a' * 64}

    def archive(self, changes=None, extra=None, duplicate=None, broken_hash=False, missing=None):
        files = {'new-horizons.exe': b'fake entry', 'Setup.exe': b'fake setup',
                 'runtime/new-horizons.exe': b'fake engine', 'runtime/Start-New-Horizons.ps1': b'# fake launcher',
                 'runtime/VCMI_lib.dll': b'fake facade', 'runtime/Launch-Process.ps1': b'# launch helper',
                 'runtime/Setup-Progress.ps1': b'# progress helper', 'runtime/Complete-Assets.ps1': b'# import helper',
                 'runtime/Player-Launcher.ps1': b'# player launcher',
                 'runtime/Player-Launcher-Operation.ps1': b'# launcher background operations',
                 'runtime/config/dirs.json': b'{}', 'runtime/config/filesystem.json': b'{}',
                 'runtime/Mods/vcmi/mod.json': b'{}', 'runtime/Mods/new-horizons/mod.json': b'{}',
                 'runtime/Mods/new-horizons/NewHorizons.nhart': struct.pack('<8sIIQQ', b'NHART\r\n\x1a', 1, 0, 32, 1),
                 'runtime/config/newHorizonsCombat.json': b'{}', 'runtime/config/schemas/newHorizonsCombat.json': b'{}',
                 'runtime/config/newHorizonsMagic.json': b'{}', 'runtime/config/schemas/newHorizonsMagic.json': b'{}',
                 'runtime/config/newHorizonsVersion.json': json.dumps({'version': '0.93.0'}).encode(),
                 'runtime/BUILD-IDENTITY.json': json.dumps({'project': 'Heroes III: New Horizons',
                     'platform': 'Windows x64', 'source_commit': self.revision}).encode()}
        files.update(changes or {})
        if missing:
            del files[missing]
        manifest = ''.join(f'{digest(data)}  {name}\n' for name, data in files.items()).encode()
        if broken_hash:
            manifest = b'0' * 64 + manifest[64:]
        files['runtime/SHA256SUMS.txt'] = manifest
        with zipfile.ZipFile(self.source, 'w', zipfile.ZIP_DEFLATED) as archive:
            stem = self.name[:-4]
            for name, data in files.items():
                archive.writestr(stem + '/' + name, data)
            if duplicate:
                archive.writestr(stem + '/' + duplicate, b'duplicate')
            for name, data, attributes in extra or []:
                entry = zipfile.ZipInfo(name)
                entry.external_attr = attributes
                archive.writestr(entry, data)
        self.offer.update(bytes=self.source.stat().st_size, sha256=digest(self.source.read_bytes()))
        self.write_inputs()

    def write_inputs(self, metadata_changes=None):
        self.request.write_text(json.dumps(self.offer))
        release = {'draft': False, 'prerelease': False, 'published_at': '2026-11-19T00:00:00Z',
                   'tag_name': 'v0.93.0', 'html_url': 'https://github.com/thegandalf196/new-horizons/releases/tag/v0.93.0',
                   'assets': [{'name': self.offer['name'], 'state': 'uploaded', 'size': self.offer['bytes'],
                       'digest': 'sha256:' + self.offer['sha256'], 'browser_download_url': self.offer['url']}]}
        release.update(metadata_changes or {})
        self.metadata.write_text(json.dumps(release))

    def execute(self, command=None, download_failure=False):
        harness = self.root / 'run.ps1'
        harness.write_text('''
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. $env:NH_UPDATE_WORKER
function Invoke-UpdateDownload([string]$URL, [string]$Output, [long]$MaximumBytes, [long]$ExactBytes = 0) {
    [IO.File]::AppendAllText($env:NH_UPDATE_DOWNLOAD_LOG, $URL + [Environment]::NewLine)
    if ($URL -eq $script:ReleaseAPI) { [IO.File]::Copy($env:NH_UPDATE_METADATA, $Output); return }
    if ($env:NH_UPDATE_FAIL -eq '1') {
        [IO.File]::WriteAllText($Output, 'interrupted download')
        throw 'Fixture interrupted transport'
    }
    [IO.File]::Copy($env:NH_UPDATE_ARCHIVE, $Output)
}
''' + (command or 'Prepare-NHReleaseUpdate $env:NH_UPDATE_STORE $env:NH_UPDATE_REQUEST | ConvertTo-Json -Compress'))
        environment = dict(os.environ, NH_UPDATE_WORKER=str(WORKER), NH_UPDATE_METADATA=str(self.metadata),
                           NH_UPDATE_ARCHIVE=str(self.source), NH_UPDATE_STORE=str(self.store),
                           NH_UPDATE_REQUEST=str(self.request), NH_UPDATE_FAIL=str(int(download_failure)),
                           NH_UPDATE_DOWNLOAD_LOG=str(self.root / 'downloads.txt'))
        return subprocess.run([POWERSHELL, '-NoLogo', '-NoProfile', '-NonInteractive', '-File', str(harness)],
                              env=environment, text=True, capture_output=True, timeout=30, check=False)

    def assert_rejected(self, **kwargs):
        result = self.execute(**kwargs)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(list((self.store / 'r').glob('*')), [])
        self.assertEqual(list(self.store.glob('.p-*')), [])

    def test_success_stages_complete_package_without_activation_or_execution(self):
        self.archive()
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ready = json.loads(result.stdout)
        self.assertEqual(ready['version'], self.offer['version'])
        self.assertEqual(ready['sha256'], self.offer['sha256'])
        # Windows PowerShell may expand an NTFS 8.3 ancestor (RUNNER~1).
        self.assertTrue(Path(ready['ready']).samefile(self.store / 'r' / self.offer['sha256'][:24]))
        self.assertEqual((Path(ready['ready']) / 'new-horizons.exe').read_bytes(), b'fake entry')
        self.assertFalse((self.store / 'current.json').exists())
        self.assertEqual(list(self.store.glob('.p-*')), [])
        repeated = self.execute()
        self.assertEqual(repeated.returncode, 0, repeated.stdout + repeated.stderr)
        self.assertEqual(json.loads(repeated.stdout), ready)
        self.assertEqual((self.root / 'downloads.txt').read_text().splitlines(), [
            'https://api.github.com/repos/thegandalf196/new-horizons/releases/latest', self.offer['url']] * 2)
        self.assertEqual((Path(ready['ready']) / 'new-horizons.exe').read_bytes(), b'fake entry')

    def test_modified_partial_and_extra_ready_files_are_rejected_without_repair(self):
        self.archive()
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ready = Path(json.loads(result.stdout)['ready'])
        (ready / 'new-horizons.exe').write_bytes(b'modified entry')
        rejected = self.execute()
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual((ready / 'new-horizons.exe').read_bytes(), b'modified entry')
        (ready / 'new-horizons.exe').write_bytes(b'fake entry')
        (ready / 'extra.txt').write_text('unlisted')
        rejected = self.execute()
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual((ready / 'extra.txt').read_text(), 'unlisted')
        (ready / 'extra.txt').unlink()
        (ready / 'Setup.exe').unlink()
        rejected = self.execute()
        self.assertNotEqual(rejected.returncode, 0)
        self.assertFalse((ready / 'Setup.exe').exists())
        self.assertEqual(list(self.store.glob('.p-*')), [])

    def test_valid_but_different_payload_in_short_destination_is_rejected(self):
        self.archive()
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        old_ready = Path(json.loads(result.stdout)['ready'])
        self.archive(changes={'runtime/new-horizons.exe': b'different release bytes'})
        collision = self.store / 'r' / self.offer['sha256'][:24]
        shutil.copytree(old_ready, collision)
        rejected = self.execute()
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual((collision / 'runtime/new-horizons.exe').read_bytes(), b'fake engine')
        self.assertEqual(list(self.store.glob('.p-*')), [])

    def test_short_stage_strips_the_archive_stem_under_classic_windows_limit(self):
        self.store = self.root / 'portable' / '.nhu'
        member = ('runtime/licenses/dependencies/boost_1.83.0/source_folder/src/libs/geometry/'
                  'doc/src/docutils/tools/doxygen_xml2qbk/contrib/rapidxml-1.13/license.txt')
        stem = self.name[:-4]
        old_path = self.store.resolve() / ('.prepare-' + 'x' * 32) / 'unpacked' / stem / member
        self.assertGreaterEqual(len(str(old_path)), 260)
        self.archive(changes={member: b'{}'}, extra=[(stem + '/', b'', 0)])
        ready = self.store.resolve() / 'r' / self.offer['sha256'][:24]
        self.assertLess(len(str(ready / member)), 260)
        result = self.execute("$env:OS = 'Windows_NT'; Prepare-NHReleaseUpdate $env:NH_UPDATE_STORE $env:NH_UPDATE_REQUEST | ConvertTo-Json -Compress")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(Path(json.loads(result.stdout)['ready']).samefile(ready))
        self.assertEqual((ready / member).read_bytes(), b'{}')
        self.assertFalse((ready / stem).exists())
        self.assertEqual(list(self.store.glob('.p-*')), [])

    def test_valid_manifest_cannot_omit_launch_dependencies(self):
        for missing in ('runtime/Launch-Process.ps1', 'runtime/Setup-Progress.ps1',
                        'runtime/Complete-Assets.ps1', 'runtime/Player-Launcher.ps1',
                        'runtime/Player-Launcher-Operation.ps1', 'runtime/config/dirs.json',
                        'runtime/config/filesystem.json', 'runtime/VCMI_lib.dll',
                        'runtime/Mods/vcmi/mod.json', 'runtime/Mods/new-horizons/mod.json',
                        'runtime/Mods/new-horizons/NewHorizons.nhart',
                        'runtime/config/newHorizonsCombat.json', 'runtime/config/schemas/newHorizonsCombat.json',
                        'runtime/config/newHorizonsMagic.json', 'runtime/config/schemas/newHorizonsMagic.json'):
            with self.subTest(missing=missing):
                self.archive(missing=missing)
                self.assert_rejected()

    def test_invalid_nhart_header_is_rejected_with_a_valid_manifest(self):
        for header in (b'not an archive', struct.pack('<8sIIQQ', b'NHART\r\n\x1a', 2, 0, 32, 1),
                       struct.pack('<8sIIQQ', b'NHART\r\n\x1a', 1, 0, 999, 1)):
            with self.subTest(header=header):
                self.archive(changes={'runtime/Mods/new-horizons/NewHorizons.nhart': header})
                self.assert_rejected()


    def test_ready_destination_over_classic_limit_is_rejected_and_cleaned(self):
        self.store = self.root / ('boundary-' + 'x' * 32) / '.nhu'
        ready = self.store.resolve() / 'r' / ('a' * 24)
        padding = 260 - len(str(ready / 'runtime' / 'limit.txt')) - 1
        self.assertGreater(padding, 0)
        member = 'runtime/' + 'd' * padding + '/limit.txt'
        self.assertEqual(len(str(ready / member)), 260)
        self.assertLessEqual(len(self.name[:-4] + '/' + member), 240)
        self.archive(changes={member: b'bounded'})
        result = self.execute("$env:OS = 'Windows_NT'; Prepare-NHReleaseUpdate $env:NH_UPDATE_STORE $env:NH_UPDATE_REQUEST | ConvertTo-Json -Compress")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ready update path would exceed Windows path limits', result.stderr)
        self.assertEqual(list((self.store / 'r').glob('*')), [])
        self.assertEqual(list(self.store.glob('.p-*')), [])

    def test_invalid_zip_constructor_releases_archive_handle(self):
        self.archive()
        self.source.write_bytes(b'not a ZIP archive')
        result = self.execute("""
$failed = $false
try { Expand-UpdateArchive $env:NH_UPDATE_ARCHIVE (Join-Path $env:NH_UPDATE_STORE 'p') 'fixture' }
catch { $failed = $true }
if (-not $failed) { throw 'Invalid archive unexpectedly opened' }
$exclusive = [IO.File]::Open($env:NH_UPDATE_ARCHIVE, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try { Write-Output 'released archive handle' } finally { $exclusive.Dispose() }
""")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('released archive handle', result.stdout)


    def test_existing_member_target_failure_closes_archive_handle(self):
        self.archive()
        self.store.mkdir()
        existing = self.store / 'new-horizons.exe'
        existing.write_bytes(b'preserve existing target')
        result = self.execute("""
$failed = $false
$offer = Read-UpdateRequest $env:NH_UPDATE_REQUEST
try { Expand-UpdateArchive $env:NH_UPDATE_ARCHIVE $env:NH_UPDATE_STORE ([IO.Path]::GetFileNameWithoutExtension($offer.name)) }
catch { $failed = $true }
if (-not $failed) { throw 'Existing member unexpectedly overwritten' }
$exclusive = [IO.File]::Open($env:NH_UPDATE_ARCHIVE, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try { Write-Output 'released failed member' } finally { $exclusive.Dispose() }
""")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('released failed member', result.stdout)
        self.assertEqual(existing.read_bytes(), b'preserve existing target')

    @unittest.skipIf(os.name == 'nt', 'POSIX symlink fixture')
    def test_linked_ready_directory_is_rejected_even_when_empty(self):
        self.archive()
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ready = Path(json.loads(result.stdout)['ready'])
        other = self.root / 'unrelated'
        other.mkdir()
        (ready / 'linked-directory').symlink_to(other, target_is_directory=True)
        rejected = self.execute()
        self.assertNotEqual(rejected.returncode, 0)
        self.assertTrue((ready / 'linked-directory').is_symlink())
        self.assertEqual(list(other.iterdir()), [])

    def test_outer_digest_mismatch_leaves_no_ready_installation(self):
        self.archive()
        with self.source.open('ab') as output:
            output.write(b'x')
        self.assert_rejected()

    def test_manifest_hash_failure_is_not_published(self):
        self.archive(broken_hash=True)
        self.assert_rejected()
        self.archive()
        with zipfile.ZipFile(self.source, 'a') as archive:
            archive.writestr(self.name[:-4] + '/unlisted.txt', b'not in inventory')
        self.offer.update(bytes=self.source.stat().st_size, sha256=digest(self.source.read_bytes()))
        self.write_inputs()
        self.assert_rejected()

    def test_unsafe_archive_members_are_rejected(self):
        stem = self.name[:-4]
        for name, attributes in [(stem + '/../outside.txt', 0), ('other-root/outside.txt', 0),
                                 (stem + '/runtime/a:b', 0),
                                 (stem + '/runtime/CON.txt', 0), (stem + '/runtime/trailing.', 0),
                                 (stem + '/runtime/link', 0o120777 << 16)]:
            with self.subTest(name=name):
                self.archive(extra=[(name, b'outside', attributes)])
                self.assert_rejected()
        self.assertFalse((self.root / 'outside.txt').exists())

    def test_duplicate_case_variant_archive_entry_is_rejected(self):
        self.archive(duplicate='SETUP.exe')
        self.assert_rejected()

    def test_truncated_zip_and_duplicate_request_are_rejected(self):
        self.archive()
        self.source.write_bytes(self.source.read_bytes()[:-15])
        self.offer.update(bytes=self.source.stat().st_size, sha256=digest(self.source.read_bytes()))
        self.write_inputs()
        self.assert_rejected()
        self.archive()
        self.request.write_text(self.request.read_text().replace('"format": 1', '"format": 1, "format": 1'))
        self.assert_rejected()

    @unittest.skipIf(os.name == 'nt', 'POSIX symlink fixture')
    def test_linked_store_is_rejected_without_touching_destination(self):
        self.archive()
        other = self.root / 'other-store'
        other.mkdir()
        self.store.symlink_to(other, target_is_directory=True)
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(other.iterdir()), [])

    def test_wrong_version_or_revision_is_rejected(self):
        for changes in [{'runtime/config/newHorizonsVersion.json': b'{"version":"0.92.0"}'},
                        {'runtime/BUILD-IDENTITY.json': json.dumps({'project': 'Heroes III: New Horizons',
                         'platform': 'Windows x64', 'source_commit': 'b' * 40}).encode()}]:
            with self.subTest(changes=changes):
                self.archive(changes=changes)
                self.assert_rejected()

    def test_release_change_and_ambiguous_archive_are_rejected(self):
        self.archive()
        for changes in [{'draft': True}, {'prerelease': True}, {'tag_name': 'v0.94.0'}]:
            with self.subTest(changes=changes):
                self.write_inputs(changes)
                self.assert_rejected()
        release = json.loads(self.metadata.read_text())
        release['tag_name'] = 'v0.93.0'
        release['assets'].append(dict(release['assets'][0]))
        self.metadata.write_text(json.dumps(release))
        self.assert_rejected()

    def test_partial_download_is_cleaned_without_replacing_existing_content(self):
        self.archive()
        self.store.mkdir()
        sentinel = self.store / 'current.json'
        sentinel.write_text('old selection')
        self.assert_rejected(download_failure=True)
        self.assertEqual(sentinel.read_text(), 'old selection')

    def test_plain_path_check_accepts_absent_parents_and_rejects_file_ancestors(self):
        self.archive()
        command = "Assert-UpdatePlainPath (Join-Path $env:NH_UPDATE_STORE 'missing/child')"
        self.assertEqual(self.execute(command).returncode, 0)
        self.assertFalse(self.store.exists())
        self.store.write_bytes(b'existing file')
        self.assertNotEqual(self.execute(command).returncode, 0)
        self.assertEqual(self.store.read_bytes(), b'existing file')

    def test_advertised_and_expanded_size_limits_are_enforced(self):
        self.archive()
        self.offer['bytes'] = 2147483649
        self.write_inputs()
        self.assert_rejected()
        self.archive()
        result = self.execute('$script:MaximumExpandedBytes = 5; Prepare-NHReleaseUpdate $env:NH_UPDATE_STORE $env:NH_UPDATE_REQUEST')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list((self.store / 'r').glob('*')), [])

    def test_only_public_https_github_hosts_can_be_redirect_targets(self):
        self.archive()
        for url in ['http://github.com/a', 'https://github.com.evil.test/a',
                    'https://user:password@github.com/a', 'https://github.com:8443/a',
                    'https://release-assets.githubusercontent.com.evil.test/a']:
            with self.subTest(url=url):
                command = 'Assert-UpdateTransportURI ([Uri]' + repr(url) + ')'
                self.assertNotEqual(self.execute(command).returncode, 0)
        self.assertEqual(self.execute("Assert-UpdateTransportURI ([Uri]'https://release-assets.githubusercontent.com/a?token=fixture')").returncode, 0)


if __name__ == '__main__':
    unittest.main()
