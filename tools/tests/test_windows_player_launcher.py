"""Offline PowerShell fixtures for the visible Windows launcher (no native UI)."""
# SPDX-License-Identifier: GPL-2.0-or-later
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PS = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')


@unittest.skipUnless(PS, 'Set NH_TEST_POWERSHELL for offline launcher checks')
class PlayerLauncherTest(unittest.TestCase):
    def execute(self, body):
        with tempfile.TemporaryDirectory() as temporary:
            harness = Path(temporary) / 'harness.ps1'
            harness.write_text(r'''
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$env:LOCALAPPDATA=Join-Path $env:FIXTURE 'local-data'
$runtime=Join-Path $env:FIXTURE 'runtime'
$null=[IO.Directory]::CreateDirectory((Join-Path $runtime 'config'))
[IO.File]::WriteAllText((Join-Path $runtime 'config/newHorizonsVersion.json'),'{"version":"0.92.3"}')
. (Join-Path $env:SOURCE 'tools/windows/Launch-Process.ps1')
. (Join-Path $env:SOURCE 'tools/windows/Player-Launcher.ps1')
. (Join-Path $env:SOURCE 'tools/windows/Player-Launcher-Operation.ps1')
function Require([bool]$Condition,[string]$Message) { if(-not $Condition){throw $Message} }
function New-FixtureView {
    $form=[PSCustomObject]@{Hidden=0;Shown=0}
    $form | Add-Member ScriptMethod Hide { $this.Hidden++ }
    $form | Add-Member ScriptMethod Show { $this.Shown++ }
    return [PSCustomObject]@{Form=$form;Version=[PSCustomObject]@{Text=''};
        Status=[PSCustomObject]@{Text=''};Bar=[PSCustomObject]@{Visible=$false};
        Play=[PSCustomObject]@{Enabled=$true};Check=[PSCustomObject]@{Enabled=$true};
        Assets=[PSCustomObject]@{Enabled=$true};Install=[PSCustomObject]@{Enabled=$false};
        State=[PSCustomObject]@{Runtime=$runtime;Offer=$null;Busy=$false;ExitCode=0}}
}
''' + body, encoding='ascii')
            result = subprocess.run([PS, '-NoProfile', '-NonInteractive', '-File', str(harness)],
                                    env=dict(os.environ, SOURCE=str(ROOT), FIXTURE=temporary),
                                    text=True, capture_output=True, timeout=25, check=False)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            return result.stdout

    def test_check_validates_official_metadata_and_cleans_temporary_json(self):
        self.execute(r'''
$metadata=Join-Path $env:FIXTURE 'metadata.json'
$sha='a'*64
$name='New-Horizons-Windows-x64-0123456789ab.zip'
$release=@{draft=$false;prerelease=$false;tag_name='v0.92.4';published_at='2026-11-18T00:00:00Z';
    html_url=$script:ReleaseRoot+'tag/v0.92.4';assets=@(@{name=$name;state='uploaded';size=100;
    digest='sha256:'+$sha;browser_download_url=$script:ReleaseRoot+'download/v0.92.4/'+$name})}
$script:ReleaseText=$release|ConvertTo-Json -Depth 5
function Invoke-UpdateDownload($URL,$Output,$MaximumBytes){[IO.File]::WriteAllText($Output,$script:ReleaseText)}
$offer=Get-NHLatestPlayerOffer $metadata
Require ($offer.sha256 -ceq $sha -and $offer.version -ceq '0.92.4') 'Wrong offer'
Require (-not [IO.File]::Exists($metadata+'.offer')) 'Validation scratch survived'
$release.prerelease=$true
$script:ReleaseText=$release|ConvertTo-Json -Depth 5
[IO.File]::Delete($metadata)
$rejected=$false
try{$null=Get-NHLatestPlayerOffer $metadata}catch{$rejected=$true}
Require $rejected 'Prerelease accepted'
''')

    def test_install_requires_explicit_offer_and_busy_prevents_reentry(self):
        self.execute(r'''
$view=New-FixtureView
function New-NHPlayerProcess { throw 'Child should never start' }
Invoke-NHPlayerUpdate $view $runtime $true
Require (-not $view.State.Busy) 'Empty offer changed state'
$view.State.Busy=$true
Invoke-NHPlayerUpdate $view $runtime $false
Require ($view.State.Busy) 'Reentrant check ran'
''')

    def test_background_check_exposes_offer_without_installing(self):
        self.execute(r'''
$view=New-FixtureView
$script:Calls=0
function New-NHPlayerProcess($Runtime,$Script,$Arguments){
    Require ($Arguments -contains 'Check') 'Not a check'
    Require (-not ($Arguments -contains '-Request')) 'Check accepted an update'
    $script:Output=$Arguments[[Array]::IndexOf($Arguments,'-Output')+1]
    $p=[PSCustomObject]@{Disposed=$false}
    $p|Add-Member ScriptMethod Dispose {$this.Disposed=$true}
    return $p
}
function Wait-NHPlayerProcess {
    $script:Calls++
    [IO.File]::WriteAllText($script:Output,'{"ok":true,"value":{"format":1,"version":"0.92.4","sha256":"accepted"}}')
    return 0
}
Invoke-NHPlayerUpdate $view $runtime $false
Require ($script:Calls -eq 1 -and $view.Install.Enabled) 'Offer missing'
Require ($view.State.Runtime -ceq $runtime) 'Check activated update'
Require (-not $view.State.Busy -and $view.Play.Enabled -and -not $view.Bar.Visible) 'Controls not restored'
''')

    def test_failed_operation_preserves_runtime_and_restores_controls(self):
        self.execute(r'''
$view=New-FixtureView
function New-NHPlayerProcess($Runtime,$Script,$Arguments){
    $script:Output=$Arguments[[Array]::IndexOf($Arguments,'-Output')+1]
    $p=[PSCustomObject]@{}
    $p|Add-Member ScriptMethod Dispose {}
    return $p
}
function Wait-NHPlayerProcess {
    [IO.File]::WriteAllText($script:Output,'{"ok":false,"error":"Offline"}')
    return 1
}
Invoke-NHPlayerUpdate $view $runtime $false
Require ($view.Status.Text.Contains('Offline')) 'Failure hidden'
Require ($view.State.Runtime -ceq $runtime -and $view.Play.Enabled) 'Fallback lost'
''')

    def test_play_and_setup_each_start_one_owned_child_and_preserve_status(self):
        self.execute(r'''
$view=New-FixtureView
$script:Launches=0
function New-NHPlayerProcess($Runtime,$Script,$Arguments){
    $script:Launches++
    if($script:Launches -eq 1){Require ($Arguments.Count -eq 0) 'Play args changed'}
    else{Require ($Arguments -contains '-SetupOnly' -and $Arguments -contains '-SelectAssets') 'Setup changed'}
    $p=[PSCustomObject]@{}
    $p|Add-Member ScriptMethod Dispose {}
    return $p
}
function Wait-NHPlayerProcess { return 37 }
Invoke-NHPlayerGame $view $false
Require ($view.State.ExitCode -eq 37) 'Exit status lost'
Invoke-NHPlayerGame $view $true
Require ($script:Launches -eq 2 -and $view.Form.Hidden -eq 2 -and $view.Form.Shown -eq 2) 'Duplicate or hidden child'
Require (-not $view.State.Busy -and $view.Play.Enabled) 'Game controls not restored'
''')

    def test_child_environment_is_isolated_and_suppresses_legacy_update(self):
        self.execute(r'''
# Inspect the actual constructed start info without spawning Windows binaries.
$definition=(Get-Command New-NHPlayerProcess).Definition.Replace('[Diagnostics.Process]::Start($info)','$info')
$builder=[ScriptBlock]::Create($definition)
$env:NH_LAUNCHER_MANAGES_UPDATES=$null
$env:NH_PORTABLE_UPDATE_REQUEST='retired'
$info=& $builder $runtime 'launcher script.ps1' @('one argument','quote"test','')
Require ($info.EnvironmentVariables['NH_LAUNCHER_MANAGES_UPDATES'] -ceq '1') 'Child still checks in game'
Require (-not $info.EnvironmentVariables.ContainsKey('NH_PORTABLE_UPDATE_REQUEST')) 'Retired request leaked'
Require ($env:NH_PORTABLE_UPDATE_REQUEST -ceq 'retired' -and -not $env:NH_LAUNCHER_MANAGES_UPDATES) 'Parent env changed'
Require ($info.UseShellExecute -eq $false -and $info.CreateNoWindow) 'Unsafe shell launch'
Require ($info.EnvironmentVariables['NH_WINDOWS_PORTABLE_UPDATE_CHILD'] -ceq $runtime) 'Recursive launcher'
''')

    def test_install_lock_wraps_acceptance_and_releases_after_failure(self):
        self.execute(r'''
function Install-NHRequestedRelease($Original,$Current,$Store,$Request,$StatusPath) {
    Require ($Request -ceq 'accepted.json' -and $StatusPath -ceq 'progress.txt') 'Request changed'
    $blocked=$false
    try { $other=[IO.File]::Open((Join-Path $Store 'lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$other.Dispose() }
    catch { $blocked=$true }
    Require $blocked 'Activation ran without exclusive lock'
    throw 'Mock failed authentication'
}
$rejected=$false
try{$null=Install-NHPlayerOffer $runtime 'accepted.json' 'progress.txt'}catch{$rejected=$true}
Require $rejected 'Install failure hidden'
$store=Get-NHUpdateStore $runtime
$other=[IO.File]::Open((Join-Path $store 'lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$other.Dispose()
Require (-not [IO.File]::Exists((Join-Path $store 'current.json'))) 'Failure activated a release'
''')

    def test_owned_check_timeout_kills_only_check_and_finishes_wait(self):
        self.execute(r'''
$definition=(Get-Command Wait-NHPlayerProcess).Definition.Replace('[Windows.Forms.Application]::DoEvents()','')
$wait=[ScriptBlock]::Create($definition)
$process=[PSCustomObject]@{HasExited=$false;Kills=0;FinalWaits=0;ExitCode=0}
$process|Add-Member ScriptMethod WaitForExit {param($milliseconds)
    if($null -eq $milliseconds){$this.FinalWaits++;return}
    [Threading.Thread]::Sleep(100)
    return $false
}
$process|Add-Member ScriptMethod Kill {$this.Kills++;$this.HasExited=$true}
$timeout=$false
try{$null=& $wait $process (New-FixtureView) $null 1}catch{$timeout=$_.Exception.Message.Contains('timed out')}
Require ($timeout -and $process.Kills -eq 1 -and $process.FinalWaits -eq 1) 'Owned deadline lifecycle wrong'
''')

    def test_auto_check_preference_does_not_change_manual_controls(self):
        self.execute(r'''
$env:LOCALAPPDATA=$env:FIXTURE
$directory=Join-Path $env:LOCALAPPDATA 'HeroesIII-NewHorizons/config'
$null=[IO.Directory]::CreateDirectory($directory)
[IO.File]::WriteAllText((Join-Path $directory 'settings.json'),'{"general":{"updateChecks":false}}')
Require (-not (Test-NHLauncherAutoCheck)) 'Opt out ignored'
[IO.File]::WriteAllText((Join-Path $directory 'settings.json'),'{"general":{"updateChecks":true}}')
Require (Test-NHLauncherAutoCheck) 'Opt in ignored'
''')

    def test_native_entry_routes_gui_only_for_plain_exe_launch(self):
        source = (ROOT / 'tools/windows/Start-New-Horizons.ps1').read_text()
        self.assertIn("$env:NH_WINDOWS_BOOTSTRAP_UI -eq '1' -and $gameArguments.Count -eq 0", source)
        self.assertIn('-not $SetupOnly -and -not $SelectAssets -and -not $AssetsPath', source)
        self.assertNotIn('NH_PORTABLE_UPDATE_REQUEST', source)
        selector = (ROOT / 'tools/windows/Portable-Release-Launcher.ps1').read_text()
        read_only = selector[selector.index('function Select-NHPortableRelease'):]
        self.assertNotIn('Install-NHRequestedRelease', read_only)


if __name__ == '__main__':
    unittest.main()
