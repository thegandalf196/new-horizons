"""Exercise Windows portable activation with an offline prepared-package fixture."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / 'tools/windows/Portable-Release-Launcher.ps1'
POWERSHELL = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')
# The self-test child is always Windows PowerShell 5.1 beside the running host.
WINDOWS_POWERSHELL = os.name == 'nt' and bool(POWERSHELL) and Path(POWERSHELL).stem.lower() == 'powershell'
SHA256 = 'c' * 64
SELF_TEST = "param([string]$Operation)\nif ($Operation -ceq 'SelfTest') { [Console]::Out.WriteLine('NH_WINDOWS_PORTABLE_LAUNCHER_V2') }\n"
TRACK_HARNESS = r'''
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Assert-PlainPath([string]$Path) {
    if ((Test-Path -LiteralPath $Path) -and ((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Linked test path.'
    }
}
. $env:TEST_PROCESS
. $env:TEST_HELPER
function Test-NHUpdatedClient([string]$Runtime) { }
function Test-Rejected([scriptblock]$Action) { try { $null = & $Action; return $false } catch { return $true } }
'''


@unittest.skipUnless(POWERSHELL, 'Set NH_TEST_POWERSHELL to run offline PowerShell fixtures')
class WindowsPortableReleaseLauncherTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.original = self.root / 'runtime'
        self.local_data = self.root / 'ld'
        self.local_data.mkdir()
        self.make_runtime(self.original, '0.92.3')
        locator = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-Command',
                                  '. $env:TEST_HELPER; Get-NHUpdateStore $env:TEST_RUNTIME $true'],
                                 env=dict(os.environ, TEST_HELPER=str(LAUNCHER), TEST_RUNTIME=str(self.original),
                                          LOCALAPPDATA=str(self.local_data)),
                                 text=True, capture_output=True, timeout=20, check=False)
        self.assertEqual(locator.returncode, 0, locator.stderr)
        self.store = Path(locator.stdout.strip())
        self.updated = self.store / 'r' / SHA256[:24] / 'runtime'
        self.request = self.store / 'request.json'
        self.selector = self.store / 'current.json'
        self.harness = self.root / 'harness.ps1'
        self.harness.write_text(r'''
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Assert-PlainPath([string]$Path) {
    if ((Test-Path -LiteralPath $Path) -and ((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Linked test path.'
    }
}
. $env:TEST_HELPER
function Test-NHUpdatedClient([string]$Runtime) {
    if ($env:TEST_PROBE -eq 'fail') { throw 'Mock client probe failure.' }
}
if ($env:TEST_UPDATE -eq 'true') {
    try { $null = Install-NHRequestedRelease $env:TEST_RUNTIME (Select-NHPortableRelease $env:TEST_RUNTIME $false).runtime $env:TEST_STORE $env:TEST_REQUEST }
    catch { }
}
Select-NHPortableRelease $env:TEST_RUNTIME $false | ConvertTo-Json -Compress
''', encoding='ascii')

    def make_runtime(self, runtime, version):
        (runtime / 'config').mkdir(parents=True)
        (runtime / 'config/newHorizonsVersion.json').write_text(json.dumps({'version': version}))
        for name in ('new-horizons.exe', 'VCMI_lib.dll', 'Start-New-Horizons.ps1', 'BUILD-IDENTITY.json',
                     'Launch-Process.ps1', 'Setup-Progress.ps1', 'Complete-Assets.ps1',
                     'Player-Launcher.ps1', 'Player-Launcher-Operation.ps1',
                     'config/dirs.json', 'config/filesystem.json', 'config/newHorizonsCombat.json',
                     'config/schemas/newHorizonsCombat.json', 'config/newHorizonsMagic.json',
                     'config/schemas/newHorizonsMagic.json', 'Mods/vcmi/mod.json',
                     'Mods/new-horizons/mod.json', 'Mods/new-horizons/NewHorizons.nhart'):
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('Fixture only')

    def offer_update(self, version='0.92.4', digest=SHA256):
        updated = self.store / 'r' / digest[:24] / 'runtime'
        self.make_runtime(updated, version)
        prepared = json.dumps({'version': version, 'sha256': digest})
        self.request.write_text(prepared)
        (self.original / 'Prepare-NHReleaseUpdate.ps1').write_text(
            f"param([string]$Store,[string]$Request,[string]$StatusPath)\n'{prepared}'\n")
        return updated

    def select(self, update=True, probe='pass'):
        env = dict(os.environ, TEST_HELPER=str(LAUNCHER), TEST_RUNTIME=str(self.original),
                   TEST_PROBE=probe, TEST_UPDATE=str(update).lower(), TEST_STORE=str(self.store), LOCALAPPDATA=str(self.local_data),
                   TEST_REQUEST=str(self.request))
        result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-File', str(self.harness)],
                                env=env, capture_output=True, text=True, check=False, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def run_tracks(self, body, **environment):
        harness = self.root / 'tracks.ps1'
        harness.write_text(TRACK_HARNESS + body, encoding='ascii')
        env = dict(os.environ, TEST_HELPER=str(LAUNCHER), TEST_PROCESS=str(LAUNCHER.with_name('Launch-Process.ps1')),
                   TEST_RUNTIME=str(self.original), TEST_STORE=str(self.store), TEST_REQUEST=str(self.request),
                   LOCALAPPDATA=str(self.local_data), **environment)
        result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-File', str(harness)],
                                env=env, capture_output=True, text=True, check=False, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def test_beta_install_selects_only_the_beta_track(self):
        updated = self.offer_update()
        self.request.write_text(json.dumps({'version': '0.92.4', 'sha256': SHA256, 'format': 2, 'channel': 'beta'}))
        report = self.run_tracks(r'''
$before = Get-NHTrackRuntime $env:TEST_RUNTIME 'beta'
$stable = Get-NHTrackRuntime $env:TEST_RUNTIME 'stable'
$installed = Install-NHRequestedRelease $env:TEST_RUNTIME $stable $env:TEST_STORE $env:TEST_REQUEST
[IO.File]::WriteAllText($env:TEST_REQUEST, '{"version":"0.92.6","sha256":"' + ('e' * 64) + '","channel":"nightly"}')
$unknown = Test-Rejected { Install-NHRequestedRelease $env:TEST_RUNTIME $stable $env:TEST_STORE $env:TEST_REQUEST }
[ordered]@{before=$before;installed=$installed;stable=(Get-NHTrackRuntime $env:TEST_RUNTIME 'stable');
    beta=(Get-NHTrackRuntime $env:TEST_RUNTIME 'beta');unknown=$unknown} | ConvertTo-Json -Compress
''')
        self.assertIsNone(report['before'])
        self.assertTrue(Path(report['installed']).samefile(updated))
        self.assertEqual(Path(report['stable']), self.original)
        self.assertTrue(Path(report['beta']).samefile(updated))
        self.assertTrue(report['unknown'])
        self.assertFalse(self.selector.exists())
        self.assertEqual(json.loads((self.store / 'beta.json').read_text()),
                         {'format': 1, 'version': '0.92.4', 'sha256': SHA256})

    def make_beta_original(self, version='0.93.1'):
        (self.original / 'config/newHorizonsVersion.json').write_text(json.dumps({'version': version}))
        (self.original / 'BUILD-IDENTITY.json').write_text(json.dumps({'source_commit': 'a' * 40, 'release_channel': 'beta'}))

    def test_package_channel_reads_the_release_marker_and_defaults_to_stable(self):
        report = self.run_tracks(r'''
$values=@()
foreach ($text in @('Fixture only','{}','{"release_channel":"stable"}','{"release_channel":"Beta"}','{"release_channel":"beta"}')) {
    [IO.File]::WriteAllText((Join-Path $env:TEST_RUNTIME 'BUILD-IDENTITY.json'), $text)
    $values += Get-NHPackageChannel $env:TEST_RUNTIME
}
[IO.File]::Delete((Join-Path $env:TEST_RUNTIME 'BUILD-IDENTITY.json'))
$values += Get-NHPackageChannel $env:TEST_RUNTIME
ConvertTo-Json -InputObject @{values=$values} -Compress
''')
        self.assertEqual(report['values'], ['stable', 'stable', 'stable', 'stable', 'beta', 'stable'])

    def test_beta_original_is_the_beta_track_and_installs_an_older_stable_beside_it(self):
        self.make_beta_original()
        stable = self.offer_update('0.92.8')
        self.request.write_text(json.dumps({'version': '0.92.8', 'sha256': SHA256}))
        report = self.run_tracks(r'''
$before=[ordered]@{stable=(Get-NHTrackRuntime $env:TEST_RUNTIME 'stable');beta=(Get-NHTrackRuntime $env:TEST_RUNTIME 'beta');
    selected=(Select-NHPortableRelease $env:TEST_RUNTIME $false).runtime;newer=(Get-NHNewerLauncher $env:TEST_RUNTIME)}
$current=(Select-NHPortableRelease $env:TEST_RUNTIME $false).runtime
$installed=Install-NHRequestedRelease $env:TEST_RUNTIME $current $env:TEST_STORE $env:TEST_REQUEST
[ordered]@{before=$before;installed=$installed;stable=(Get-NHTrackRuntime $env:TEST_RUNTIME 'stable');
    beta=(Get-NHTrackRuntime $env:TEST_RUNTIME 'beta');selected=(Select-NHPortableRelease $env:TEST_RUNTIME $false).runtime;
    newer=(Get-NHNewerLauncher $env:TEST_RUNTIME)} | ConvertTo-Json -Compress -Depth 4
''')
        self.assertEqual(report['before'], {'stable': None, 'beta': str(self.original), 'selected': str(self.original),
                                            'newer': None})
        self.assertTrue(Path(report['installed']).samefile(stable))
        self.assertTrue(Path(report['stable']).samefile(stable))
        self.assertEqual(Path(report['beta']), self.original)
        # The older stable launcher never takes over the beta installation.
        self.assertEqual(Path(report['selected']), self.original)
        self.assertIsNone(report['newer'])
        self.assertFalse((self.store / 'beta.json').exists())
        self.assertEqual(json.loads(self.selector.read_text()), {'format': 1, 'version': '0.92.8', 'sha256': SHA256})

    def test_beta_original_installs_only_newer_betas_and_hands_over_to_them(self):
        self.make_beta_original()
        newer = self.offer_update('0.93.2')
        report = self.run_tracks(r'''
function Test-NHLauncherSelfTest([string]$Runtime) { return $true }
$current=(Select-NHPortableRelease $env:TEST_RUNTIME $false).runtime
[IO.File]::WriteAllText($env:TEST_REQUEST, '{"version":"0.93.1","sha256":"' + ('c' * 64) + '","channel":"beta"}')
$same=Install-NHRequestedRelease $env:TEST_RUNTIME $current $env:TEST_STORE $env:TEST_REQUEST
$sameSelected=[IO.File]::Exists((Join-Path $env:TEST_STORE 'beta.json'))
[IO.File]::WriteAllText($env:TEST_REQUEST, '{"version":"0.93.2","sha256":"' + ('c' * 64) + '","channel":"beta"}')
$installed=Install-NHRequestedRelease $env:TEST_RUNTIME $current $env:TEST_STORE $env:TEST_REQUEST
$selected=(Select-NHPortableRelease $env:TEST_RUNTIME $false).runtime
[ordered]@{same=$same;sameSelected=$sameSelected;installed=$installed;selected=$selected;
    stable=(Get-NHTrackRuntime $env:TEST_RUNTIME 'stable');newer=(Get-NHNewerLauncher $env:TEST_RUNTIME);
    resolved=(Resolve-NHLauncherOriginal $selected $env:TEST_RUNTIME)} | ConvertTo-Json -Compress
''')
        self.assertEqual(Path(report['same']), self.original)
        self.assertFalse(report['sameSelected'])
        self.assertTrue(Path(report['installed']).samefile(newer))
        self.assertTrue(Path(report['selected']).samefile(newer))
        self.assertIsNone(report['stable'])
        self.assertTrue(Path(report['newer']).samefile(newer / 'Start-New-Horizons.ps1'))
        self.assertTrue(Path(report['resolved']).samefile(self.original))
        self.assertFalse(self.selector.exists())

    @unittest.skipUnless(WINDOWS_POWERSHELL, 'The launcher self-test child is Windows PowerShell 5.1')
    def test_launcher_hands_over_to_the_selected_stable_launcher_after_its_self_test(self):
        hand_over = "$newer = Get-NHNewerLauncher $env:TEST_RUNTIME; ConvertTo-Json -InputObject @{newer=$newer} -Compress"
        self.assertIsNone(self.run_tracks(hand_over)['newer'])
        updated = self.offer_update()
        self.assertEqual(Path(self.select()['runtime']), updated)
        operation = updated / 'Player-Launcher-Operation.ps1'
        operation.write_text(SELF_TEST)
        self.assertTrue(Path(self.run_tracks(hand_over)['newer']).samefile(updated / 'Start-New-Horizons.ps1'))
        for broken in ('exit 3\n', "[Console]::Out.WriteLine('something else')\n", SELF_TEST + 'exit 1\n'):
            with self.subTest(broken=broken):
                operation.write_text(broken)
                self.assertIsNone(self.run_tracks(hand_over)['newer'])
        operation.write_text('Start-Sleep -Seconds 30\n')
        started = time.monotonic()
        report = self.run_tracks("ConvertTo-Json -InputObject @{passed=(Test-NHLauncherSelfTest $env:TEST_UPDATED 1000)} -Compress",
                                 TEST_UPDATED=str(updated))
        self.assertFalse(report['passed'])
        self.assertLess(time.monotonic() - started, 20)

    def test_delegated_launcher_belongs_to_the_selected_update_and_runs_its_own_worker(self):
        self.offer_update()
        updated = Path(self.select()['runtime'])
        code = self.root / 'newer-code'
        code.mkdir()
        newer = self.offer_update('0.92.5', 'd' * 64)
        prepared = json.dumps({'version': '0.92.5', 'sha256': 'd' * 64})
        (code / 'Prepare-NHReleaseUpdate.ps1').write_text(
            "param([string]$Store,[string]$Request,[string]$StatusPath)\n"
            "[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'used.txt'), 'newer worker')\n'" + prepared + "'\n")
        (self.original / 'Prepare-NHReleaseUpdate.ps1').write_text("throw 'The original worker ran'\n")
        report = self.run_tracks(r'''
$original = Resolve-NHLauncherOriginal $env:TEST_UPDATED $env:TEST_RUNTIME
$plain = Resolve-NHLauncherOriginal $env:TEST_RUNTIME ''
$self = Test-Rejected { Resolve-NHLauncherOriginal $env:TEST_RUNTIME $env:TEST_RUNTIME }
$other = Test-Rejected { Resolve-NHLauncherOriginal $env:TEST_CODE $env:TEST_RUNTIME }
$relative = Test-Rejected { Resolve-NHLauncherOriginal $env:TEST_UPDATED 'runtime' }
$installed = Install-NHRequestedRelease $env:TEST_RUNTIME $env:TEST_UPDATED $env:TEST_STORE $env:TEST_REQUEST $null $env:TEST_CODE
[ordered]@{original=$original;plain=$plain;rejected=@($self,$other,$relative);installed=$installed} | ConvertTo-Json -Compress
''', TEST_UPDATED=str(updated), TEST_CODE=str(code))
        self.assertTrue(Path(report['original']).samefile(self.original))
        self.assertTrue(Path(report['plain']).samefile(self.original))
        self.assertEqual(report['rejected'], [True, True, True])
        self.assertTrue(Path(report['installed']).samefile(newer))
        self.assertEqual((code / 'used.txt').read_text(), 'newer worker')

    def test_successful_probe_activates_only_the_verified_directory(self):
        self.offer_update()
        self.assertEqual(Path(self.select()['runtime']), self.updated)
        self.assertEqual(Path(self.select(update=False)['runtime']), self.updated)
        self.assertTrue(self.request.exists())
        self.assertTrue((self.original / 'new-horizons.exe').exists())

    def test_failed_probe_keeps_original_and_preserves_accepted_request(self):
        self.offer_update()
        self.assertEqual(Path(self.select(probe='fail')['runtime']), self.original)
        self.assertFalse(self.selector.exists())
        self.assertTrue(self.request.exists())

    def test_second_update_replaces_selector_only_after_readiness_checks(self):
        self.offer_update()
        self.assertEqual(Path(self.select()['runtime']), self.updated)
        newer = self.offer_update('0.92.5', 'd' * 64)
        previous = self.selector.read_bytes()
        missing = newer / 'Complete-Assets.ps1'
        original = missing.read_bytes()
        missing.unlink()
        self.assertEqual(Path(self.select()['runtime']), self.updated)
        self.assertEqual(self.selector.read_bytes(), previous)
        missing.write_bytes(original)
        self.request.write_text(json.dumps({'version': '0.92.5', 'sha256': 'd' * 64}))
        self.assertEqual(Path(self.select()['runtime']), newer)
        self.assertEqual(Path(self.select(update=False)['runtime']), newer)

    def test_retired_pending_request_is_never_applied_by_selection(self):
        self.offer_update()
        self.assertEqual(Path(self.select(update=False)['runtime']), self.original)
        self.assertTrue(self.request.exists())
        self.assertFalse(self.selector.exists())

    def test_help_or_setup_probe_does_not_create_update_state(self):
        (self.store / 'installation.json').unlink()
        self.store.rmdir()
        self.store.parent.rmdir()
        self.assertEqual(Path(self.select(update=False)['runtime']), self.original)
        self.assertFalse(self.store.exists())


if __name__ == '__main__':
    unittest.main()
