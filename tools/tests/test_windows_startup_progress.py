"""Exercise exact-child startup progress without showing host windows or starting a game."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
POWERSHELL = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')


@unittest.skipUnless(POWERSHELL, 'Set NH_TEST_POWERSHELL for non-graphical PowerShell fixtures')
class WindowsStartupProgressTest(unittest.TestCase):
    def execute(self, body, ui='1'):
        with tempfile.TemporaryDirectory() as temporary:
            harness = Path(temporary) / 'progress.ps1'
            harness.write_text(r'''
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $env:NH_PROGRESS_ROOT 'tools/windows/Setup-Progress.ps1')
. (Join-Path $env:NH_PROGRESS_ROOT 'tools/windows/Launch-Process.ps1')
$script:ProgressCreated = 0
$script:PumpCount = 0
function New-FixtureProgress {
    $form = [PSCustomObject]@{Text='Import';Tag=$false;IsDisposed=$false;CloseCalls=0}
    $form | Add-Member ScriptMethod Close { $this.CloseCalls++ }
    $form | Add-Member ScriptMethod Dispose { $this.IsDisposed=$true }
    return [PSCustomObject]@{Form=$form;Label=[PSCustomObject]@{Text='Import'};
        Bar=[PSCustomObject]@{Style='Blocks';MarqueeAnimationSpeed=0}}
}
function Show-AssetImportProgress([int]$Files,[long]$Bytes,[switch]$StartingGame) {
    $script:ProgressCreated++
    return New-FixtureProgress
}
function Update-GameStartupProgress($Progress) { $script:PumpCount++ }
function New-FixtureProcess([int]$GameAt=2,[bool]$ThrowProbe=$false) {
    $process = [PSCustomObject]@{GameAt=$GameAt;ThrowProbe=$ThrowProbe;RefreshCalls=0;
        MainWindowHandle=[IntPtr]123;MainWindowTitle='Console';TimedWaits=0;FinalWaits=0;ClosedAtWait=$false}
    $process | Add-Member ScriptMethod Refresh {
        $this.RefreshCalls++
        if($this.ThrowProbe){throw 'Window read raced process exit.'}
        if($this.RefreshCalls -ge $this.GameAt){$this.MainWindowTitle='New Horizons'}
    }
    $process | Add-Member ScriptMethod WaitForExit {
        param($Milliseconds)
        if($PSBoundParameters.ContainsKey('Milliseconds')){
            if($Milliseconds -ne 100){throw 'Wrong polling interval.'}
            $this.TimedWaits++
            return $this.TimedWaits -ge 4
        }
        $this.FinalWaits++
        if($null -ne $script:ObservedProgress){$this.ClosedAtWait=$script:ObservedProgress.Form.IsDisposed}
    }
    return $process
}
$script:ObservedProgress=$null
''' + body, encoding='ascii')
            env = dict(os.environ, NH_PROGRESS_ROOT=str(ROOT), NH_WINDOWS_BOOTSTRAP_UI=ui,
                       NH_PROGRESS_POWERSHELL=POWERSHELL)
            result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-File', str(harness)],
                                    env=env, capture_output=True, text=True, check=False, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads(result.stdout)

    def test_import_dialog_transitions_to_indeterminate_game_startup(self):
        seen = self.execute(r'''
$imported=New-FixtureProgress
$p=Show-GameStartupProgress $imported @()
[PSCustomObject]@{same=[Object]::ReferenceEquals($imported,$p);title=$p.Form.Text;
    text=$p.Label.Text;style=$p.Bar.Style;speed=$p.Bar.MarqueeAnimationSpeed;
    created=$script:ProgressCreated;pumps=$script:PumpCount}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen, {'same': True, 'title': 'New Horizons - starting game',
                               'text': 'Preparing game artwork and starting New Horizons...',
                               'style': 'Marquee', 'speed': 30, 'created': 0, 'pumps': 1})

    def test_warm_ordinary_exe_launch_creates_the_same_progress(self):
        seen = self.execute(r'''
$p=Show-GameStartupProgress $null @()
[PSCustomObject]@{created=$script:ProgressCreated;text=$p.Label.Text}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen['created'], 1)
        self.assertEqual(seen['text'], 'Preparing game artwork and starting New Horizons...')

    def test_cli_headless_and_report_arguments_suppress_progress(self):
        for args in (['--headless'], ['--version'], ['--help'], ['--report-bug', 'pending'], ['--nointro']):
            with self.subTest(args=args):
                psargs = ','.join("'" + arg + "'" for arg in args)
                seen = self.execute(r'''
$old=New-FixtureProgress
$p=Show-GameStartupProgress $old @(''' + psargs + r''')
[PSCustomObject]@{suppressed=($null -eq $p);created=$script:ProgressCreated;closed=$old.Form.IsDisposed}|ConvertTo-Json -Compress
''')
                self.assertEqual(seen, {'suppressed': True, 'created': 0, 'closed': True})
        seen = self.execute(r'''
$p=Show-GameStartupProgress $null @()
[PSCustomObject]@{suppressed=($null -eq $p);created=$script:ProgressCreated}|ConvertTo-Json -Compress
''', ui='')
        self.assertEqual(seen, {'suppressed': True, 'created': 0})

    def test_exact_owned_game_title_closes_progress_before_final_child_wait(self):
        seen = self.execute(r'''
$script:ObservedProgress=New-FixtureProgress
$child=New-FixtureProcess
Wait-NativeClientExit $child $script:ObservedProgress
[PSCustomObject]@{polls=$child.TimedWaits;waits=$child.FinalWaits;closed=$child.ClosedAtWait;
    closes=$script:ObservedProgress.Form.CloseCalls;pumps=$script:PumpCount}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen, {'polls': 2, 'waits': 1, 'closed': True, 'closes': 1, 'pumps': 1})

    def test_console_and_racing_title_read_never_discard_child_wait(self):
        for throwing in ('$false', '$true'):
            with self.subTest(throwing=throwing):
                seen = self.execute(r'''
$script:ObservedProgress=New-FixtureProgress
$child=New-FixtureProcess 99 ''' + throwing + r'''
Wait-NativeClientExit $child $script:ObservedProgress
[PSCustomObject]@{polls=$child.TimedWaits;waits=$child.FinalWaits;closed=$child.ClosedAtWait;
    closes=$script:ObservedProgress.Form.CloseCalls}|ConvertTo-Json -Compress
''')
                self.assertEqual(seen, {'polls': 4, 'waits': 1, 'closed': True, 'closes': 1})

    def test_presentation_failure_closes_dialog_and_keeps_ordinary_wait(self):
        seen = self.execute(r'''
function Update-GameStartupProgress($Progress){throw 'Optional presentation failed.'}
$script:ObservedProgress=New-FixtureProgress
$child=New-FixtureProcess 99
Wait-NativeClientExit $child $script:ObservedProgress
[PSCustomObject]@{polls=$child.TimedWaits;waits=$child.FinalWaits;closed=$child.ClosedAtWait}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen, {'polls': 1, 'waits': 1, 'closed': True})

    def test_managed_readiness_uses_event_instead_of_early_window_title(self):
        seen = self.execute(r'''
$script:ObservedProgress=New-FixtureProgress
$child=New-FixtureProcess 1
$handle=[PSCustomObject]@{Polls=0}
$handle|Add-Member ScriptMethod WaitOne {param($milliseconds) $this.Polls++;return $this.Polls -ge 2}
Wait-NativeClientExit $child $script:ObservedProgress ([PSCustomObject]@{Handle=$handle})
[PSCustomObject]@{polls=$child.TimedWaits;titleReads=$child.RefreshCalls;eventPolls=$handle.Polls;
    waits=$child.FinalWaits;closed=$child.ClosedAtWait}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen, {'polls': 2, 'titleReads': 0, 'eventPolls': 2, 'waits': 1, 'closed': True})

    def test_owned_event_env_and_handle_survive_until_actual_child_exit(self):
        seen = self.execute(r'''
$env:NH_LAUNCHER_MANAGES_UPDATES='1'
$env:NH_LAUNCHER_READY_EVENT='parent-untrusted-value'
$script:Handle=[PSCustomObject]@{Disposed=$false}
$script:Handle|Add-Member ScriptMethod WaitOne {param($milliseconds) return $true}
$script:Handle|Add-Member ScriptMethod Dispose {$this.Disposed=$true}
function New-NativeClientReadySignal {return [PSCustomObject]@{Name='Local\NewHorizonsLauncherReady-0123456789abcdef0123456789abcdef';Handle=$script:Handle}}
$script:ObservedProgress=New-FixtureProgress
$command="if (`$env:NH_LAUNCHER_READY_EVENT -ceq 'Local\NewHorizonsLauncherReady-0123456789abcdef0123456789abcdef') { Start-Sleep -Milliseconds 250; exit 37 } else { exit 99 }"
$code=Invoke-NativeClient $env:NH_PROGRESS_POWERSHELL ([IO.Path]::GetTempPath()) @('-NoProfile','-NonInteractive','-Command',$command) $script:ObservedProgress
[PSCustomObject]@{code=$code;disposed=$script:Handle.Disposed;parent=$env:NH_LAUNCHER_READY_EVENT;
    closed=$script:ObservedProgress.Form.IsDisposed}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen, {'code': 37, 'disposed': True, 'parent': 'parent-untrusted-value', 'closed': True})

    def test_failed_child_start_disposes_owned_event_and_progress(self):
        seen = self.execute(r'''
$env:NH_LAUNCHER_MANAGES_UPDATES='1'
$script:Handle=[PSCustomObject]@{Disposed=$false}
$script:Handle|Add-Member ScriptMethod Dispose {$this.Disposed=$true}
function New-NativeClientReadySignal {return [PSCustomObject]@{Name='Local\NewHorizonsLauncherReady-0123456789abcdef0123456789abcdef';Handle=$script:Handle}}
$script:ObservedProgress=New-FixtureProgress
$failed=$false
try{$null=Invoke-NativeClient (Join-Path ([IO.Path]::GetTempPath()) 'NH-no-such-program.exe') ([IO.Path]::GetTempPath()) @() $script:ObservedProgress}catch{$failed=$true}
[PSCustomObject]@{failed=$failed;disposed=$script:Handle.Disposed;closed=$script:ObservedProgress.Form.IsDisposed}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen, {'failed': True, 'disposed': True, 'closed': True})

    def test_plain_launch_uses_no_polling_and_preserves_actual_exit_code(self):
        seen = self.execute(r'''
$child=New-FixtureProcess
Wait-NativeClientExit $child $null
$code=Invoke-NativeClient $env:NH_PROGRESS_POWERSHELL ([IO.Path]::GetTempPath()) @('-NoProfile','-NonInteractive','-Command','exit 37')
[PSCustomObject]@{polls=$child.TimedWaits;waits=$child.FinalWaits;code=$code}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen, {'polls': 0, 'waits': 1, 'code': 37})

    def test_failed_process_start_closes_progress_without_losing_the_error(self):
        seen = self.execute(r'''
$script:ObservedProgress=New-FixtureProgress
$failed=$false
try { $null=Invoke-NativeClient (Join-Path ([IO.Path]::GetTempPath()) 'NH-no-such-program.exe') ([IO.Path]::GetTempPath()) @() $script:ObservedProgress }
catch {$failed=$true}
[PSCustomObject]@{failed=$failed;closed=$script:ObservedProgress.Form.IsDisposed;
    closes=$script:ObservedProgress.Form.CloseCalls}|ConvertTo-Json -Compress
''')
        self.assertEqual(seen, {'failed': True, 'closed': True, 'closes': 1})


if __name__ == '__main__':
    unittest.main()
