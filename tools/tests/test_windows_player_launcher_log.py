"""Offline PowerShell fixtures for the Windows launcher session log and update-check deadline."""
# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PS = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')
HARNESS = r'''
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$env:USERPROFILE=Join-Path (Join-Path $env:FIXTURE 'Users') 'player'
$env:LOCALAPPDATA=Join-Path (Join-Path $env:USERPROFILE 'AppData') 'Local'
$env:APPDATA=Join-Path (Join-Path $env:USERPROFILE 'AppData') 'Roaming'
$env:TEMP=Join-Path $env:LOCALAPPDATA 'Temp'
$sep=[IO.Path]::DirectorySeparatorChar
$runtime=Join-Path $env:FIXTURE 'runtime'
$null=[IO.Directory]::CreateDirectory((Join-Path $runtime 'config'))
[IO.File]::WriteAllText((Join-Path $runtime 'config/newHorizonsVersion.json'),'{"version":"0.93.1"}')
. (Join-Path $env:SOURCE 'tools/windows/Launch-Process.ps1')
. (Join-Path $env:SOURCE 'tools/windows/Player-Launcher.ps1')
. (Join-Path $env:SOURCE 'tools/windows/Player-Launcher-Operation.ps1')
function Require([bool]$Condition,[string]$Message) { if(-not $Condition){throw $Message} }
function Read-Log { return [IO.File]::ReadAllText((Get-NHLauncherLogPath)) }
function New-FixtureView {
    $form=[PSCustomObject]@{Hidden=0;Shown=0}
    $form | Add-Member ScriptMethod Hide { $this.Hidden++ }
    $form | Add-Member ScriptMethod Show { $this.Shown++ }
    return [PSCustomObject]@{Form=$form;Version=[PSCustomObject]@{Text=''};
        Status=[PSCustomObject]@{Text=''};Bar=[PSCustomObject]@{Visible=$false};
        Play=[PSCustomObject]@{Enabled=$true};Check=[PSCustomObject]@{Enabled=$true};
        Assets=[PSCustomObject]@{Enabled=$true};Install=[PSCustomObject]@{Enabled=$false};
        Track=[PSCustomObject]@{Enabled=$true;SelectedIndex=0};Notes=[PSCustomObject]@{Enabled=$true};
        State=[PSCustomObject]@{Runtime=$runtime;Offer=$null;Busy=$false;ExitCode=0;Track='stable';Own='stable';
            Runtimes=@{stable=$runtime;beta=$null};Code=(Join-Path $env:SOURCE 'tools/windows');Notes=@{};
            Checking=$false;PlayRequested=$false;CloseRequested=$false;Launches=0}}
}
function New-RunningProcess {
    $process=[PSCustomObject]@{HasExited=$false;Kills=0;FinalWaits=0;ExitCode=0;Id=4242}
    $process|Add-Member ScriptMethod WaitForExit {param($milliseconds)
        if($null -eq $milliseconds){$this.FinalWaits++;return}
        [Threading.Thread]::Sleep(50)
        return $this.HasExited
    }
    $process|Add-Member ScriptMethod Kill {$this.Kills++;$this.HasExited=$true}
    return $process
}
$definition=(Get-Command Wait-NHPlayerProcess).Definition.Replace('[Windows.Forms.Application]::DoEvents()','')
$script:Wait=[ScriptBlock]::Create($definition)
'''


@unittest.skipUnless(PS, 'Set NH_TEST_POWERSHELL for offline launcher checks')
class PlayerLauncherLogTest(unittest.TestCase):
    def execute(self, body):
        with tempfile.TemporaryDirectory() as temporary:
            harness = Path(temporary) / 'harness.ps1'
            harness.write_text(HARNESS + body, encoding='ascii')
            result = subprocess.run([PS, '-NoProfile', '-NonInteractive', '-File', str(harness)],
                                    env=dict(os.environ, SOURCE=str(ROOT), FIXTURE=temporary),
                                    text=True, capture_output=True, timeout=60, check=False)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            return result.stdout

    def test_check_deadline_is_generous_and_each_request_has_its_own_bound(self):
        self.execute(r'''
Require ($script:NHCheckDeadlineSeconds -eq 45) 'Check deadline changed'
Require ($script:NHCheckRequestMilliseconds -eq 20000) 'Request bound changed'
Require ($script:NHCheckRequestMilliseconds / 1000 -lt $script:NHCheckDeadlineSeconds) 'Request bound exceeds the deadline'
$script:Timeouts=@()
function Invoke-UpdateDownload($URL,$Output,$MaximumBytes,$ExactBytes,$TimeoutMilliseconds){
    $script:Timeouts+=$TimeoutMilliseconds;[IO.File]::WriteAllText($Output,'[]')}
$StatusPath=Join-Path $env:FIXTURE 'status.txt'
$null=Get-NHPublishedRelease (Join-Path $env:FIXTURE 'list.json') 'beta'
Require ($script:Timeouts.Count -eq 1 -and $script:Timeouts[0] -eq 20000) 'Check request has no own bound'
Require ([IO.File]::ReadAllText($StatusPath) -ceq 'phase:release') 'Phase not reported'
$source=[IO.File]::ReadAllText((Join-Path $env:SOURCE 'tools/windows/Player-Launcher.ps1'))
Require ($source.Contains('$timeout = if ($Install) { 0 } else { $script:NHCheckDeadlineSeconds }')) 'Install gained a deadline'
''')

    def test_timeout_names_the_phase_the_child_was_in_and_logs_it(self):
        self.execute(r'''
Start-NHLauncherSession $runtime
$status=Join-Path $env:FIXTURE 'status.txt'
[IO.File]::WriteAllText($status,'phase:request')
$view=New-FixtureView
$process=New-RunningProcess
$failure=$null
try{$null=& $script:Wait $process $view $status 1}catch{$failure=$_}
Require ($failure.Exception -is [TimeoutException]) 'Not a timeout'
Require ($failure.Exception.Message -ceq 'Update check timed out while contacting GitHub after 1 s. You can still play the installed game.') $failure.Exception.Message
Require ($process.Kills -eq 1 -and $process.FinalWaits -eq 1) 'Owned deadline lifecycle wrong'
Require ($view.Status.Text -ceq 'Contacting GitHub...') 'Phase text not shown'
Show-NHPlayerUpdateFailure $view 'Check' $failure
$hint=' Details: %LOCALAPPDATA%' + $sep + 'HeroesIII-NewHorizons' + $sep + 'logs' + $sep + 'launcher.log'
Require ($view.Status.Text -ceq $failure.Exception.Message + $hint) $view.Status.Text
$log=Read-Log
Require ($log.Contains('Check deadline of 1 s reached') -and $log.Contains('in phase request')) 'Timeout not logged'
Require ($log.Contains('System.TimeoutException: Update check timed out')) 'Failure type not logged'
[IO.File]::Delete($status)
$failure=$null
try{$null=& $script:Wait (New-RunningProcess) $view $status 1}catch{$failure=$_}
Require ($failure.Exception.Message.Contains('while starting PowerShell for the update check')) 'Unstarted child not named'
''')

    def test_play_during_a_check_stops_it_and_starts_the_game(self):
        self.execute(r'''
$view=New-FixtureView
$script:Games=0
function New-NHPlayerProcess($Runtime,$Script,$Arguments){
    Require ($Arguments -contains '-LogPath') 'Child has no log path'
    $p=New-RunningProcess
    $p|Add-Member ScriptMethod Dispose {}
    return $p
}
function Invoke-NHPlayerGame($View,$Setup){ $script:Games++; Require (-not $Setup) 'Setup started' }
function Wait-NHPlayerProcess($Process,$View,$StatusPath,$TimeoutSeconds){
    Require ($View.State.Busy -and $View.State.Checking -and $View.Play.Enabled) 'Play blocked by the check'
    Require (-not $View.Check.Enabled -and -not $View.Track.Enabled) 'Check controls enabled'
    Request-NHPlayerGame $View
    Require ($View.State.PlayRequested -and -not $View.Play.Enabled) 'Play request lost'
    Require ($script:Games -eq 0) 'Game started inside the check wait'
    return & $script:Wait $Process $View $StatusPath $TimeoutSeconds
}
Invoke-NHPlayerUpdate $view $runtime $false
Require ($script:Games -eq 1 -and -not $view.State.PlayRequested -and -not $view.State.Busy) 'Game not started after the check stopped'
Require (-not $view.Status.Text.Contains('Update unavailable')) 'Stopped check reported a failure'
$view.State.Offer=[PSCustomObject]@{version='0.93.2'}
function Wait-NHPlayerProcess($Process,$View){ Require (-not $View.Play.Enabled) 'Play enabled during install'; return 1 }
Invoke-NHPlayerUpdate $view $runtime $true
Require ($view.Status.Text.Contains('stopped without a result (exit code 1)')) $view.Status.Text
''')

    def test_closing_during_a_check_stops_it_then_closes_the_window(self):
        self.execute(r'''
$view=New-FixtureView
$view.Form|Add-Member NoteProperty Closed 0
$view.Form|Add-Member ScriptMethod Close { $this.Closed++ }
function New-NHPlayerProcess { $p=New-RunningProcess; $p|Add-Member ScriptMethod Dispose {}; return $p }
function Wait-NHPlayerProcess($Process,$View,$StatusPath,$TimeoutSeconds){
    $closing=[PSCustomObject]@{Cancel=$false}
    Request-NHPlayerClose $View $closing
    Require ($closing.Cancel -and $View.State.CloseRequested) 'Busy window closed or close lost'
    return & $script:Wait $Process $View $StatusPath $TimeoutSeconds
}
Invoke-NHPlayerUpdate $view $runtime $false
Require ($view.Form.Closed -eq 1 -and -not $view.State.Busy) 'Window not closed after the stopped check'
Require (-not $view.Status.Text.Contains('Update unavailable')) 'Stopped check reported a failure'
$idle=[PSCustomObject]@{Cancel=$false}
Request-NHPlayerClose $view $idle
Require (-not $idle.Cancel) 'Idle window kept open'
''')

    def test_session_log_rotates_redacts_personal_folders_and_is_bounded(self):
        self.execute(r'''
$path=Get-NHLauncherLogPath
Require ($path -ceq ([IO.Path]::GetFullPath($env:LOCALAPPDATA) + $sep + 'HeroesIII-NewHorizons' + $sep + 'logs' + $sep + 'launcher.log')) $path
Start-NHLauncherSession $runtime
Write-NHLauncherLog ('Profile ' + $env:USERPROFILE + '\Downloads and temp ' + $env:TEMP + '\x' + "`nsecond")
$first=Read-Log
Require ($first -cmatch '(?m)^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} [+-]\d\d:\d\d \+\d+\.\d{3}s launcher +New Horizons launcher 0\.93\.1 \(Windows\)') $first
Require ($first.Contains('PowerShell ') -and $first.Contains('.NET Framework 4 release')) 'Header incomplete'
Require ($first.Contains('Profile %USERPROFILE%\Downloads') -and $first.Contains('temp %TEMP%\x | second')) $first
Require (-not $first.Contains($env:USERPROFILE)) 'Profile path leaked'
$user=[Environment]::UserName
Require ((Protect-NHLogText ('D:\Users\' + $user + '\Saves')) -ceq 'D:\Users\%USERNAME%\Saves' -or $user.Length -lt 3) 'User name kept'
Require (([regex]::Matches($first,"`r`n")).Count -ge 4) 'Header lines missing'
# Fewer known folders keep whole-path entries, longest first.
$savedTemp=$env:TEMP;$env:TEMP=$null;$env:APPDATA=$null
Set-NHLogRedactions
$names=@($script:NHLogRedactions | ForEach-Object { $_.Name })
Require ($names -ccontains '%USERPROFILE%' -and $names -ccontains '%LOCALAPPDATA%') ($names -join ',')
Require ((Protect-NHLogText ($env:LOCALAPPDATA + '\x')) -ceq '%LOCALAPPDATA%\x') 'Longest folder not first'
$env:TEMP=$savedTemp
# A hand-over continues the same session; a new launcher start rotates it.
$script:NHLogPath=''
Start-NHLauncherSession $runtime
Require ((Read-Log).StartsWith($first) -and (Read-Log).Contains('Continuing in the launcher')) 'Hand-over rotated the log'
$script:NHLogPath=''
$env:NH_LAUNCHER_LOG_SESSION=$null
Start-NHLauncherSession $runtime
$previous=Join-Path ([IO.Path]::GetDirectoryName($path)) 'launcher.previous.log'
Require ([IO.File]::ReadAllText($previous).StartsWith($first)) 'Previous session not kept'
Require (-not (Read-Log).Contains('Profile %USERPROFILE%')) 'New session did not start a new log'
$script:NHLogMaximumBytes=4096
for($i=0;$i -lt 200;$i++){ Write-NHLauncherLog ('line ' + $i + ' ' + ('x'*40)) }
$bounded=Read-Log
Require ((New-Object IO.FileInfo($path)).Length -lt 5000) 'Log not bounded'
Require (([regex]::Matches($bounded,'Log size limit reached')).Count -eq 1) 'Limit marker missing or repeated'
''')

    def test_child_appends_its_phases_and_failed_request_to_the_session_log(self):
        self.execute(r'''
Start-NHLauncherSession $runtime
$session=$script:NHLogPath
$StatusPath=Join-Path $env:FIXTURE 'status.txt'
$script:NHLogPath=''
Open-NHLauncherLog $session 'check'
Set-NHCheckPhase $StatusPath 'request'
$failed=$false
try { Invoke-UpdateDownload 'https://example.invalid/releases?token=secret' (Join-Path $env:FIXTURE 'out.json') 1024 } catch { $failed=$true }
Require $failed 'Untrusted host accepted'
$log=Read-Log
Require ($log.Contains('launcher New Horizons launcher')) 'Child replaced the session log'
Require ($log -cmatch '(?m) check +Phase: request\r?$') 'Child phase missing'
Require ($log.Contains('HTTP request to example.invalid/releases failed after') -and $log.Contains('Unsupported update transport destination')) $log
Require (-not $log.Contains('token=secret')) 'URL query logged'
Require ([IO.File]::ReadAllText($StatusPath) -ceq 'phase:request') 'Phase file wrong'
$timeout=New-Object Net.WebException('The operation has timed out.', [Net.WebExceptionStatus]::Timeout)
$wrapped=New-Object Management.Automation.MethodInvocationException('Exception calling "GetResponse"', $timeout)
$record=New-Object Management.Automation.ErrorRecord($wrapped, 'fixture', 'NotSpecified', $null)
Require ((Get-NHOperationError $record) -ceq 'Could not reach GitHub (Timeout): The operation has timed out.') (Get-NHOperationError $record)
''')


if __name__ == '__main__':
    unittest.main()
