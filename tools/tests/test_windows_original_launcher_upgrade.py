"""Offline fixtures for upgrading a pre-hand-over Windows original launcher."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = ROOT / 'tools/windows'
POWERSHELL = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')
SHA256 = 'c' * 64
LAUNCHER_FILES = ('Release-Metadata.ps1', 'Launch-Process.ps1', 'Setup-Progress.ps1', 'Complete-Assets.ps1',
                  'Prepare-NHReleaseUpdate.ps1', 'Player-Launcher-Notes.ps1', 'Player-Launcher.ps1',
                  'Player-Launcher-Operation.ps1', 'Update-NHOriginalLauncher.ps1', 'Portable-Release-Launcher.ps1',
                  'Start-New-Horizons.ps1')
# Launcher scripts of a 0.92.4/0.92.5 original that differ from the current set.
OLD_FILES = ('Prepare-NHReleaseUpdate.ps1', 'Player-Launcher.ps1', 'Player-Launcher-Operation.ps1',
             'Portable-Release-Launcher.ps1', 'Start-New-Horizons.ps1')
HARNESS = r'''
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Assert-PlainPath([string]$Path) {
    if ((Test-Path -LiteralPath $Path) -and ((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Linked test path.'
    }
}
. (Join-Path $env:TEST_SOURCE 'Launch-Process.ps1')
. (Join-Path $env:TEST_SOURCE 'Portable-Release-Launcher.ps1')
# The real self-test child is Windows PowerShell 5.1; run the same operation here.
function Test-NHLauncherSelfTest([string]$Runtime) {
    [IO.File]::AppendAllText($env:TEST_CALLS, "self-test`n")
    if ($env:TEST_SELF_TEST -eq 'fail') { return $false }
    $operation = Join-Path $Runtime 'Player-Launcher-Operation.ps1'
    $output = & $env:TEST_POWERSHELL -NoProfile -NonInteractive -File $operation -Operation SelfTest
    return $LASTEXITCODE -eq 0 -and ($output -join '').Trim() -ceq $script:NHLauncherSelfTestToken
}
. (Join-Path $env:TEST_SOURCE 'Update-NHOriginalLauncher.ps1')
Update-NHOriginalLauncher $env:TEST_CHILD
'''


@unittest.skipUnless(POWERSHELL, 'Set NH_TEST_POWERSHELL to run offline PowerShell fixtures')
class WindowsOriginalLauncherUpgradeTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.original = self.root / 'runtime'
        self.local_data = self.root / 'ld'
        self.local_data.mkdir()
        self.calls = self.root / 'calls.txt'
        self.make_runtime(self.original, '0.92.4')
        for name in OLD_FILES:
            (self.original / name).write_text('# 0.92.4 ' + name + '\n')
        for name in ('Launch-Process.ps1', 'Setup-Progress.ps1', 'Complete-Assets.ps1'):
            shutil.copyfile(WINDOWS / name, self.original / name)
        self.store = Path(self.powershell('. $env:TEST_HELPER; Get-NHUpdateStore $env:TEST_ORIGINAL $true',
                                          TEST_HELPER=str(WINDOWS / 'Portable-Release-Launcher.ps1')).strip())
        self.updated = self.store / 'r' / SHA256[:24] / 'runtime'
        self.make_runtime(self.updated, '0.92.6')
        for name in LAUNCHER_FILES:
            shutil.copyfile(WINDOWS / name, self.updated / name)
        self.select(SHA256, '0.92.6')
        self.harness = self.root / 'harness.ps1'
        self.harness.write_text(HARNESS, encoding='ascii')

    def make_runtime(self, runtime, version):
        (runtime / 'config').mkdir(parents=True)
        (runtime / 'config/newHorizonsVersion.json').write_text(json.dumps({'version': version}))
        for name in ('new-horizons.exe', 'VCMI_lib.dll', 'BUILD-IDENTITY.json',
                     'config/dirs.json', 'config/filesystem.json', 'config/newHorizonsCombat.json',
                     'config/schemas/newHorizonsCombat.json', 'config/newHorizonsMagic.json',
                     'config/schemas/newHorizonsMagic.json', 'Mods/vcmi/mod.json',
                     'Mods/new-horizons/mod.json', 'Mods/new-horizons/NewHorizons.nhart',
                     'Start-New-Horizons.ps1', 'Launch-Process.ps1', 'Setup-Progress.ps1', 'Complete-Assets.ps1',
                     'Player-Launcher.ps1', 'Player-Launcher-Operation.ps1'):
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('Fixture only ' + version)

    def select(self, digest, version):
        (self.store / 'current.json').write_text(json.dumps({'format': 1, 'version': version, 'sha256': digest}))

    def environment(self, **extra):
        return dict(os.environ, LOCALAPPDATA=str(self.local_data), TEST_ORIGINAL=str(self.original),
                    TEST_SOURCE=str(WINDOWS), TEST_POWERSHELL=POWERSHELL, TEST_CALLS=str(self.calls), **extra)

    def powershell(self, command, **extra):
        result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-Command', command],
                                env=self.environment(**extra), text=True, capture_output=True, timeout=30, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def upgrade(self, child=None, self_test='pass'):
        result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-File', str(self.harness)],
                                env=self.environment(TEST_CHILD=str(child or self.updated), TEST_SELF_TEST=self_test),
                                text=True, capture_output=True, timeout=60, check=False)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return result

    def snapshot(self):
        return {str(path.relative_to(self.original)): path.read_bytes()
                for path in sorted(self.original.rglob('*')) if path.is_file()}

    def self_tests(self):
        return self.calls.read_text().count('self-test') if self.calls.exists() else 0

    def test_old_original_is_upgraded_once_and_keeps_a_backup(self):
        before = self.snapshot()
        result = self.upgrade()
        self.assertIn('launcher was upgraded', result.stdout)
        self.assertEqual(self.self_tests(), 1)
        replaced = set(OLD_FILES) | {'Release-Metadata.ps1', 'Update-NHOriginalLauncher.ps1', 'Player-Launcher-Notes.ps1'}
        after = self.snapshot()
        for name in LAUNCHER_FILES:
            self.assertEqual(after[name], (WINDOWS / name).read_bytes(), name)
        self.assertEqual({name for name in after if after[name] != before.get(name)}, replaced)
        backup = self.store / 'launcher-backup-0.92.4'
        record = json.loads((backup / 'backup.json').read_text())
        self.assertEqual({entry['name']: entry['existed'] for entry in record['files']},
                         {name: name in OLD_FILES for name in replaced})
        for name in OLD_FILES:
            self.assertEqual((backup / name).read_bytes(), before[name])
        self.assertEqual(sorted(path.name for path in backup.iterdir()), sorted(set(OLD_FILES) | {'backup.json'}))
        self.assertEqual(list(self.original.glob('*.nh-new-*')), [])
        self.upgrade()
        self.assertEqual(self.self_tests(), 1)
        self.assertEqual(self.snapshot(), after)

    def test_original_with_the_hand_over_is_left_untouched(self):
        shutil.copyfile(WINDOWS / 'Portable-Release-Launcher.ps1', self.original / 'Portable-Release-Launcher.ps1')
        before = self.snapshot()
        self.upgrade()
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(self.self_tests(), 0)
        self.assertFalse(any(self.store.glob('launcher-backup-*')))

    def test_failed_self_test_restores_the_previous_launcher(self):
        before = self.snapshot()
        result = self.upgrade(self_test='fail')
        self.assertIn('failed its self-test', result.stderr)
        self.assertIn('previous launcher was restored', result.stderr)
        self.assertEqual(self.snapshot(), before)
        self.assertTrue((self.store / 'launcher-backup-0.92.4' / 'backup.json').is_file())
        self.assertEqual(list(self.original.glob('*.nh-new-*')), [])
        # The attempt is recorded once; later plays never retry or spawn the self-test.
        self.upgrade()
        self.assertEqual(self.self_tests(), 1)
        self.assertEqual(self.snapshot(), before)

    def test_non_update_or_unselected_runs_change_nothing(self):
        before = self.snapshot()
        self.upgrade(child=self.original)
        other = self.store / 'r' / ('d' * 24) / 'runtime'
        self.make_runtime(other, '0.92.6')
        result = self.upgrade(child=other)
        self.assertIn('not the selected update', result.stderr)
        self.select(SHA256, '0.92.4')
        (self.updated / 'config/newHorizonsVersion.json').write_text(json.dumps({'version': '0.92.4'}))
        self.upgrade()
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(self.self_tests(), 0)
        self.assertFalse(any(self.store.glob('launcher-backup-*')))

    def test_update_child_entry_runs_the_upgrade_without_blocking_launch(self):
        source = (WINDOWS / 'Start-New-Horizons.ps1').read_text()
        start = source.index('if ($portableChild) {')
        child = source[start:source.index('if (-not $portableChild) {', start)]
        self.assertIn("Join-Path $packageRoot 'Update-NHOriginalLauncher.ps1'", child)
        self.assertIn('Update-NHOriginalLauncher $packageRoot', child)
        self.assertIn('} catch {', child)


if __name__ == '__main__':
    unittest.main()
