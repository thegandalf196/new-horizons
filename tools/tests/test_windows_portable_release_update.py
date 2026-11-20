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
# Exact deepest member of the verified public 0.92.3 Windows ZIP (147 UTF-16 units).
REAL_WINDOWS_MEMBER = ('runtime/licenses/dependencies/boost_1.83.0/source_folder/src/libs/geometry/'
                       'doc/src/docutils/tools/doxygen_xml2qbk/contrib/rapidxml-1.13/license.txt')
POWERSHELL = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')


DOWNLOAD_FIXTURE = r'''function Invoke-UpdateDownload([string]$URL, [string]$Output, [long]$MaximumBytes, [long]$ExactBytes = 0) {
    [IO.File]::AppendAllText($env:NH_UPDATE_DOWNLOAD_LOG, $URL + [Environment]::NewLine)
    if ($URL -eq $script:ReleaseAPI) { [IO.File]::Copy($env:NH_UPDATE_METADATA, $Output); return }
    if ($env:NH_UPDATE_FAIL -eq '1') {
        [IO.File]::WriteAllText($Output, 'interrupted download')
        throw 'Fixture interrupted transport'
    }
    [IO.File]::Copy($env:NH_UPDATE_ARCHIVE, $Output)
}
'''

# Serves the beta tag metadata and the archive; any other release API is an error.
BETA_TAG_FIXTURE = r'''function Invoke-UpdateDownload([string]$URL, [string]$Output, [long]$MaximumBytes, [long]$ExactBytes = 0) {
    [IO.File]::AppendAllText($env:NH_UPDATE_DOWNLOAD_LOG, $URL + [Environment]::NewLine)
    if ($URL -ceq (Get-UpdateReleaseTagAPI '0.93.0')) { [IO.File]::Copy($env:NH_UPDATE_METADATA, $Output); return }
    if ($URL.StartsWith('https://api.github.com/')) { throw 'Unexpected release metadata request' }
    [IO.File]::Copy($env:NH_UPDATE_ARCHIVE, $Output)
}
Prepare-NHReleaseUpdate $env:NH_UPDATE_STORE $env:NH_UPDATE_REQUEST | ConvertTo-Json -Compress
'''

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
                 'runtime/Portable-Release-Launcher.ps1': b'# release selector',
                 'runtime/Prepare-NHReleaseUpdate.ps1': b'# update preparer',
                 'runtime/Release-Metadata.ps1': b'# release tracks',
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

    def execute(self, command=None, download_failure=False, initialization=None, extra_env=None):
        harness = self.root / 'run.ps1'
        harness.write_text('''
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. $env:NH_UPDATE_WORKER
''' + (initialization or '') + '''
''' + DOWNLOAD_FIXTURE + (command or 'Prepare-NHReleaseUpdate $env:NH_UPDATE_STORE $env:NH_UPDATE_REQUEST | ConvertTo-Json -Compress'))
        environment = dict(os.environ, NH_UPDATE_WORKER=str(WORKER), NH_UPDATE_METADATA=str(self.metadata),
                           NH_UPDATE_ARCHIVE=str(self.source), NH_UPDATE_STORE=str(self.store),
                           NH_UPDATE_REQUEST=str(self.request), NH_UPDATE_FAIL=str(int(download_failure)),
                           NH_UPDATE_DOWNLOAD_LOG=str(self.root / 'downloads.txt'))
        environment.update(extra_env or {})
        return subprocess.run([POWERSHELL, '-NoLogo', '-NoProfile', '-NonInteractive', '-File', str(harness)],
                              env=environment, text=True, capture_output=True, timeout=30, check=False)

    def assert_rejected(self, **kwargs):
        result = self.execute(**kwargs)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(list((self.store / 'r').glob('*')), [])
        self.assertEqual(list(self.store.glob('.p-*')), [])

    @staticmethod
    def windows_units(path):
        return len(str(path).encode('utf-16-le')) // 2

    def directory_at_units(self, units):
        parent = self.root.resolve()
        name_units = units - self.windows_units(parent) - 1
        self.assertGreaterEqual(name_units, 4, 'Fixture parent leaves no room for a spaced directory')
        name = 'NH ' + 'x' * (name_units - 3)
        path = parent / name
        self.assertEqual(self.windows_units(path), units)
        path.mkdir()
        return path

    def prepare_long_install(self, short_alias=False):
        installation = self.directory_at_units(120)
        local = self.directory_at_units(64)
        original = installation / 'runtime'
        (original / 'config').mkdir(parents=True)
        (original / 'config/newHorizonsVersion.json').write_text('{"version":"0.92.3"}')
        (original / 'BUILD-IDENTITY.json').write_text('{}')
        (original / 'new-horizons.exe').write_bytes(b'original remains untouched')
        # Run the actual worker in its real script scope; replace only offline transport.
        worker = WORKER.read_text()
        guard = "if ($MyInvocation.InvocationName -ne '.') {"
        self.assertEqual(worker.count(guard), 1)
        (original / 'Prepare-NHReleaseUpdate.ps1').write_text(worker.replace(guard, DOWNLOAD_FIXTURE + guard))
        shutil.copyfile(WORKER.with_name('Release-Metadata.ps1'), original / 'Release-Metadata.ps1')
        passed_original = original
        passed_local = local
        if short_alias:
            import ctypes
            function = ctypes.WinDLL('kernel32', use_last_error=True).GetShortPathNameW
            function.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint]
            function.restype = ctypes.c_uint
            output = ctypes.create_unicode_buffer(32768)
            count = function(str(original), output, len(output))
            self.assertGreater(count, 0)
            self.assertLess(count, len(output))
            passed_original = Path(output.value)
            if os.path.normcase(str(passed_original)) == os.path.normcase(str(original)):
                self.skipTest('This Windows volume exposes no 8.3 alias for the fixture')
            self.assertTrue(passed_original.samefile(original))
            count = function(str(local), output, len(output))
            self.assertGreater(count, 0)
            self.assertLess(count, len(output))
            passed_local = Path(output.value)
            self.assertTrue(passed_local.samefile(local))
        self.archive(changes={REAL_WINDOWS_MEMBER: b'verified deepest license'})
        return installation, local, original, passed_original, passed_local

    def check_long_install_update(self, short_alias=False):
        installation, local, original, passed_original, passed_local = self.prepare_long_install(short_alias)
        original_files = {path.relative_to(original).as_posix(): path.read_bytes()
                          for path in original.rglob('*') if path.is_file()}
        command = r"""
$env:OS = 'Windows_NT'
$script:ProbeCount = 0
function Test-NHUpdatedClient([string]$Runtime) { $script:ProbeCount++ }
$result = Install-NHPlayerOffer $env:NH_UPDATE_ORIGINAL $env:NH_UPDATE_REQUEST $null
$selected = Select-NHPortableRelease $env:NH_UPDATE_ORIGINAL $false
$store = Get-NHUpdateStore $env:NH_UPDATE_ORIGINAL
$canonicalStore = Get-NHUpdateStore $env:NH_UPDATE_CANONICAL_ORIGINAL
[ordered]@{runtime=$result.runtime;selected=$selected.runtime;store=$store;canonicalStore=$canonicalStore;probes=$script:ProbeCount} | ConvertTo-Json -Compress
"""
        result = self.execute(command, initialization='. $env:NH_UPDATE_OPERATION\n',
                              extra_env={'LOCALAPPDATA': str(passed_local), 'NH_UPDATE_ORIGINAL': str(passed_original),
                                         'NH_UPDATE_CANONICAL_ORIGINAL': str(original),
                                         'NH_UPDATE_OPERATION': str(ROOT / 'tools/windows/Player-Launcher-Operation.ps1')})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads(result.stdout)
        store = Path(report['store'])
        self.assertTrue(Path(report['canonicalStore']).samefile(store))
        self.assertEqual(self.windows_units(local.resolve()), 64)
        self.assertEqual(self.windows_units(installation.resolve()), 120)
        self.assertTrue(store.parent.parent.samefile(local))
        self.assertEqual(store.parent.name, 'NH')
        self.assertRegex(store.name, r'^[a-f0-9]{16}$')
        ready = store / 'r' / self.offer['sha256'][:24]
        self.assertTrue(Path(report['runtime']).samefile(ready / 'runtime'))
        self.assertTrue(Path(report['selected']).samefile(ready / 'runtime'))
        self.assertEqual(report['probes'], 1)
        self.assertEqual((ready / REAL_WINDOWS_MEMBER).read_bytes(), b'verified deepest license')
        self.assertEqual(self.windows_units(ready / REAL_WINDOWS_MEMBER), 259)
        self.assertEqual(self.windows_units((ready / REAL_WINDOWS_MEMBER).parent), 247)
        self.assertEqual({path.relative_to(original).as_posix(): path.read_bytes()
                          for path in original.rglob('*') if path.is_file()}, original_files)
        self.assertFalse((installation / '.nhu').exists())
        self.assertTrue((store / 'current.json').is_file())
        self.assertEqual(list(store.glob('.p-*')), [])

    def test_120_unit_install_root_updates_and_activates_real_package_depth(self):
        self.check_long_install_update()

    @unittest.skipUnless(os.name == 'nt', 'Real Windows GetShortPathNameW and NTFS identity')
    def test_native_short_alias_updates_and_selects_same_120_unit_installation(self):
        self.check_long_install_update(short_alias=True)

    def test_short_payload_updates_with_canonical_local_data_over_64_units(self):
        installation, _, original, _, _ = self.prepare_long_install()
        local = self.directory_at_units(80)
        self.archive()  # The complete required payload, without the deep license fixture.
        result = self.execute(r"""
$env:OS = 'Windows_NT'
$store = Get-NHUpdateStore $env:NH_UPDATE_ORIGINAL $true
Prepare-NHReleaseUpdate $store $env:NH_UPDATE_REQUEST | ConvertTo-Json -Compress
""", extra_env={'LOCALAPPDATA': str(local), 'NH_UPDATE_ORIGINAL': str(original)})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ready = Path(json.loads(result.stdout)['ready'])
        self.assertGreater(self.windows_units(local.resolve()), 64)
        self.assertTrue(ready.parent.parent.parent.parent.samefile(local))
        self.assertEqual((ready / 'new-horizons.exe').read_bytes(), b'fake entry')
        self.assertTrue(all(self.windows_units(path) < 260 for path in ready.rglob('*')))
        self.assertEqual((original / 'new-horizons.exe').read_bytes(), b'original remains untouched')
        self.assertFalse((installation / '.nhu').exists())

    def test_abandoned_identity_temporary_cannot_block_store_retry(self):
        installation, local, original, _, _ = self.prepare_long_install()
        identity = str(installation.resolve()).upper()
        key = digest(identity.encode())[:16]
        parent = local / 'NH'
        store = parent / key
        store.mkdir(parents=True)
        abandoned = parent / ('.i-' + key + '-' + 'a' * 32)
        abandoned.write_bytes(b'partial interrupted identity')
        result = self.execute('Get-NHUpdateStore $env:NH_UPDATE_ORIGINAL $true',
                              extra_env={'LOCALAPPDATA': str(local), 'NH_UPDATE_ORIGINAL': str(original)})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(Path(result.stdout.strip()).samefile(store))
        self.assertEqual(json.loads((store / 'installation.json').read_text()),
                         {'format': 1, 'installation': identity})
        self.assertEqual(abandoned.read_bytes(), b'partial interrupted identity')
        self.assertEqual(list(parent.glob('.i-*')), [abandoned])

    def test_store_isolation_and_full_installation_identity_are_validated(self):
        installation, local, original, _, _ = self.prepare_long_install()
        other = self.root / 'other-installation' / 'runtime'
        other.mkdir(parents=True)
        result = self.execute(r"""
$first = Get-NHUpdateStore $env:NH_UPDATE_ORIGINAL $true
$second = Get-NHUpdateStore $env:NH_UPDATE_OTHER $true
[ordered]@{first=$first;second=$second} | ConvertTo-Json -Compress
""", extra_env={'LOCALAPPDATA': str(local), 'NH_UPDATE_ORIGINAL': str(original), 'NH_UPDATE_OTHER': str(other)})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        stores = json.loads(result.stdout)
        self.assertNotEqual(stores['first'], stores['second'])
        marker = Path(stores['first']) / 'installation.json'
        record = json.loads(marker.read_text())
        self.assertEqual(record['installation'], str(installation.resolve()).upper())
        record['installation'] = str(other.parent.resolve()).upper()
        marker.write_text(json.dumps(record))
        rejected = self.execute('Get-NHUpdateStore $env:NH_UPDATE_ORIGINAL $true',
                                extra_env={'LOCALAPPDATA': str(local), 'NH_UPDATE_ORIGINAL': str(original)})
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn('another installation', rejected.stderr)

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
                        'runtime/Player-Launcher-Operation.ps1', 'runtime/Portable-Release-Launcher.ps1',
                        'runtime/Prepare-NHReleaseUpdate.ps1', 'runtime/Release-Metadata.ps1', 'runtime/config/dirs.json',
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
        padding = 260 - len(str(ready / 'runtime' / 'long-limit-name.txt')) - 1
        self.assertGreater(padding, 0)
        member = 'runtime/' + 'd' * padding + '/long-limit-name.txt'
        self.assertEqual(len(str(ready / member)), 260)
        self.assertLessEqual(len(self.name[:-4] + '/' + member), 240)
        self.archive(changes={member: b'bounded'})
        result = self.execute("$env:OS = 'Windows_NT'; Prepare-NHReleaseUpdate $env:NH_UPDATE_STORE $env:NH_UPDATE_REQUEST | ConvertTo-Json -Compress")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ready update path would exceed Windows path limits', result.stderr)
        self.assertEqual(list((self.store / 'r').glob('*')), [])
        self.assertEqual(list(self.store.glob('.p-*')), [])

    def test_ready_parent_directory_limit_rejects_even_a_shorter_file(self):
        self.store = self.root / ('boundary-' + 'x' * 32) / '.nhu'
        ready = self.store.resolve() / 'r' / ('a' * 24)
        padding = 248 - self.windows_units(ready / 'runtime') - 1
        self.assertGreater(padding, 0)
        member = 'runtime/' + 'd' * padding + '/x.txt'
        self.assertEqual(self.windows_units((ready / member).parent), 248)
        self.assertLess(self.windows_units(ready / member), 260)
        for empty_directory in (False, True):
            with self.subTest(empty_directory=empty_directory):
                if empty_directory:
                    name = self.name[:-4] + '/' + member.rsplit('/', 1)[0] + '/'
                    self.archive(extra=[(name, b'', 0)])
                else:
                    self.archive(changes={member: b'bounded'})
                result = self.execute("$env:OS = 'Windows_NT'; Prepare-NHReleaseUpdate $env:NH_UPDATE_STORE $env:NH_UPDATE_REQUEST | ConvertTo-Json -Compress")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('ready update directory path would exceed Windows path limits', result.stderr)
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

    def test_beta_request_revalidates_its_own_tag_and_accepts_a_prerelease(self):
        self.offer.update(format=2, channel='beta')
        self.archive()
        self.write_inputs({'prerelease': True, 'draft': True})
        self.assert_rejected(command=BETA_TAG_FIXTURE)
        self.write_inputs({'prerelease': True})
        result = self.execute(BETA_TAG_FIXTURE)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout)['version'], '0.93.0')
        tag = 'https://api.github.com/repos/thegandalf196/new-horizons/releases/tags/v0.93.0'
        self.assertEqual((self.root / 'downloads.txt').read_text().splitlines(), [tag, tag, self.offer['url']])

    def test_track_requests_cannot_mix_formats_name_unknown_tracks_or_take_a_stable_prerelease(self):
        self.archive()
        accepted = dict(self.offer)
        for change in ({'format': 1, 'channel': 'beta'}, {'format': 2}, {'format': 2, 'channel': 'nightly'},
                       {'format': 2, 'channel': 7}):
            with self.subTest(change=change):
                self.offer = dict(accepted, **change)
                self.write_inputs()
                self.assert_rejected()
        self.offer = dict(accepted, format=2, channel='stable')
        self.write_inputs({'prerelease': True})
        self.assert_rejected()
        self.write_inputs()
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn('/tags/', (self.root / 'downloads.txt').read_text())
        self.assertNotEqual(self.execute("Get-UpdateReleaseTagAPI '0.93.0/../latest'").returncode, 0)

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
