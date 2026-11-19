"""Exercise Windows portable activation with an offline prepared-package fixture."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / 'tools/windows/Portable-Release-Launcher.ps1'
POWERSHELL = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')
SHA256 = 'c' * 64


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
