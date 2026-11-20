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
        Track=[PSCustomObject]@{Enabled=$true;SelectedIndex=0};
        State=[PSCustomObject]@{Runtime=$runtime;Offer=$null;Busy=$false;ExitCode=0;Track='stable';
            Runtimes=@{stable=$runtime;beta=$null};Code=(Join-Path $env:SOURCE 'tools/windows')}}
}
function New-FixtureRelease([string]$Version,[bool]$Prerelease,[bool]$Draft) {
    $name='New-Horizons-Windows-x64-0123456789ab.zip'
    return @{draft=$Draft;prerelease=$Prerelease;tag_name='v'+$Version;published_at='2026-11-21T12:00:00Z';
        html_url=$script:ReleaseRoot+'tag/v'+$Version;assets=@(@{name=$name;state='uploaded';size=100;
        digest='sha256:'+('a'*64);browser_download_url=$script:ReleaseRoot+'download/v'+$Version+'/'+$name})}
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

    def test_beta_check_offers_newest_published_release_including_prereleases(self):
        self.execute(r'''
$metadata=Join-Path $env:FIXTURE 'metadata.json'
$script:Downloads=@()
function Invoke-UpdateDownload($URL,$Output,$MaximumBytes){$script:Downloads+=$URL;[IO.File]::WriteAllText($Output,$script:ReleaseText)}
$list=@((New-FixtureRelease '0.92.5' $false $false),(New-FixtureRelease '0.93.1' $false $true),
    (New-FixtureRelease '0.93.0' $true $false),@{tag_name='nonsense'},'not a release')
$script:ReleaseText=ConvertTo-Json -InputObject $list -Depth 6
$offer=Get-NHLatestPlayerOffer $metadata 'beta'
Require ($script:Downloads[0] -ceq $script:ReleaseListAPI) 'Beta did not list recent releases'
Require ($offer.version -ceq '0.93.0' -and $offer.channel -ceq 'beta' -and $offer.format -eq 2) 'Wrong beta offer'
Require (-not [IO.File]::Exists($metadata+'.offer')) 'Validation scratch survived'
$script:ReleaseText=(New-FixtureRelease '0.93.0' $true $false)|ConvertTo-Json -Depth 6
$rejected=$false
try{$null=Get-NHLatestPlayerOffer $metadata}catch{$rejected=$true}
Require $rejected 'Stable accepted a prerelease'
$script:ReleaseText=ConvertTo-Json -InputObject @((New-FixtureRelease '0.92.3' $true $false)) -Depth 6
$view=New-FixtureView
$view.State.Track='beta'
$view.State.Runtime=$null
Complete-NHPlayerCheck $view (Get-NHLatestPlayerOffer $metadata 'beta')
Require ($null -eq $view.State.Offer) 'A beta not newer than stable was offered'
Require ($view.Status.Text -ceq 'No beta newer than your installed versions is available right now.') 'Wrong empty beta status'
$script:ReleaseText='[]'
Require ($null -eq (Get-NHLatestPlayerOffer $metadata 'beta')) 'Empty release list produced an offer'
Complete-NHPlayerCheck $view $offer
Require ($view.State.Offer.version -ceq '0.93.0' -and $view.Status.Text -ceq 'Beta 0.93.0 is available.') 'Beta offer hidden'
''')

    def test_beta_plays_with_separate_profile_and_reuses_stable_original_files(self):
        self.execute(r'''
$view=New-FixtureView
$beta=Join-Path $env:FIXTURE 'beta-runtime'
$view.State.Runtimes['beta']=$beta
$view.State.Track='beta'
$view.State.Runtime=$beta
$script:Calls=@()
function New-NHPlayerProcess($Runtime,$Script,$Arguments,$LocalData){
    $script:Calls+=[PSCustomObject]@{Runtime=$Runtime;Script=$Script;Arguments=(@($Arguments) -join '|');LocalData=$LocalData}
    $p=[PSCustomObject]@{}
    $p|Add-Member ScriptMethod Dispose {}
    return $p
}
function Wait-NHPlayerProcess { return 0 }
# The launcher normalizes LOCALAPPDATA; CI temp folders use 8.3 short names.
$localFull=[IO.Path]::GetFullPath($env:LOCALAPPDATA)
$betaLocal=Join-Path $localFull 'NewHorizons-Beta'
Invoke-NHPlayerGame $view $false
$stableContent=Join-Path $localFull 'HeroesIII-NewHorizons\content'
$null=[IO.Directory]::CreateDirectory($stableContent)
[IO.File]::WriteAllText((Join-Path $stableContent '.nh-assets.json'),'{}')
Invoke-NHPlayerGame $view $false
Invoke-NHPlayerGame $view $true
$own=Join-Path $betaLocal 'HeroesIII-NewHorizons\content'
$null=[IO.Directory]::CreateDirectory($own)
[IO.File]::WriteAllText((Join-Path $own '.nh-assets.json'),'{}')
Invoke-NHPlayerGame $view $false
$view.State.Runtime=$null
Invoke-NHPlayerGame $view $false
Require ($view.Status.Text.Contains('No beta is installed') -and $view.State.ExitCode -eq 1) 'Empty beta played'
Invoke-NHPlayerGame $view $true
$view.State.Track='stable'
$view.State.Runtime=$runtime
Invoke-NHPlayerGame $view $false
Require ($script:Calls.Count -eq 6) ('Wrong launches: ' + $script:Calls.Count)
foreach ($call in $script:Calls[0..4]) { Require ($call.LocalData -ceq $betaLocal) 'Beta shared the stable profile' }
Require ($script:Calls[0].Script -ceq (Join-Path $beta 'Start-New-Horizons.ps1') -and $script:Calls[0].Arguments -ceq '') 'Beta without game files'
Require ($script:Calls[1].Arguments -ceq ('-AssetsPath|' + $stableContent)) 'Stable game files not reused'
Require ($script:Calls[2].Arguments -ceq '-SelectAssets|-SetupOnly') 'Beta chooser reused stable files'
Require ($script:Calls[3].Arguments -ceq '') 'Beta own files replaced'
Require ($script:Calls[4].Runtime -ceq $runtime -and $script:Calls[4].Arguments -ceq '-SelectAssets|-SetupOnly') 'Empty beta chooser'
Require ($script:Calls[5].LocalData -ceq '' -and $script:Calls[5].Runtime -ceq $runtime) 'Stable profile moved'
''')

    def test_track_switch_rechecks_empty_beta_and_never_plays_the_original_for_it(self):
        self.execute(r'''
$view=New-FixtureView
$script:Checks=0
function Invoke-NHPlayerUpdate($View,$Original,$Install){ $script:Checks++; Require (-not $Install) 'Track switch installed' }
Set-NHPlayerTrack $view $runtime 1
Require ($view.State.Track -ceq 'beta' -and $null -eq $view.State.Runtime -and -not $view.Play.Enabled) 'Empty beta playable'
Require ($script:Checks -eq 1) 'Empty beta did not check'
Require ($view.Version.Text -ceq 'No beta installed yet. Check for updates to find one.') 'Wrong empty beta label'
Require ($view.Status.Text -ceq 'Beta builds try experimental changes and keep their own saves and settings.') 'Wrong beta status'
Set-NHPlayerBusy $view $true
Require (-not $view.Track.Enabled -and -not $view.Check.Enabled) 'Track enabled while busy'
$view.Track.SelectedIndex=0
Set-NHPlayerTrack $view $runtime 0
Require ($view.State.Track -ceq 'beta' -and $view.Track.SelectedIndex -eq 1) 'Busy launcher changed track'
Set-NHPlayerBusy $view $false
$view.State.Runtimes['beta']=$runtime
Set-NHPlayerTrack $view $runtime 0
Require ($view.State.Track -ceq 'stable' -and $view.Play.Enabled -and $view.Status.Text -ceq 'Ready to play.') 'Stable not restored'
Require ($view.Version.Text -ceq 'Installed version: 0.92.3') 'Wrong stable label'
Set-NHPlayerTrack $view $runtime 1
Require ($script:Checks -eq 1 -and $view.Play.Enabled) 'Installed beta was not playable'
Require ($view.Version.Text -ceq ('Beta 0.92.3 ' + [char]0x00B7 + ' separate saves')) 'Wrong installed beta label'
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


class PlayerLauncherHandOverSourceTest(unittest.TestCase):
    def test_original_entry_hands_over_once_to_the_self_tested_selected_launcher(self):
        source = (ROOT / 'tools/windows/Start-New-Horizons.ps1').read_text()
        ui = source[source.index("$env:NH_WINDOWS_BOOTSTRAP_UI -eq '1' -and $gameArguments.Count -eq 0"):]
        ui = ui[:ui.index('$selectedRelease = Select-NHPortableRelease')]
        # The delegated flag is consumed before any routing, so a copy never hands over again.
        self.assertLess(source.index('$env:NH_WINDOWS_LAUNCHER_DELEGATED = $null'), source.index('if (-not $portableChild)'))
        self.assertIn('$launcherOriginal = Resolve-NHLauncherOriginal $packageRoot $delegatedOriginal', ui)
        handover = ui[ui.index('if ($launcherOriginal -ceq $packageRoot)'):ui.index('$playerLauncher =')]
        self.assertIn('$newerLauncher = Get-NHNewerLauncher $packageRoot', handover)
        self.assertIn('$env:NH_WINDOWS_LAUNCHER_DELEGATED = $packageRoot', handover)
        # Same owned process: no new child outlives the bootstrap's exact wait.
        self.assertIn('& $newerLauncher', handover)
        self.assertNotIn('Process]::Start', handover)
        self.assertIn('exit (Show-NHPlayerLauncher $launcherOriginal $packageRoot)', ui)
        launcher = (ROOT / 'tools/windows/Player-Launcher.ps1').read_text()
        self.assertIn("Join-Path $View.State.Code 'Player-Launcher-Operation.ps1'", launcher)
        operation = (ROOT / 'tools/windows/Player-Launcher-Operation.ps1').read_text()
        self.assertIn('Install-NHPlayerOffer $Original $Request $StatusPath $PSScriptRoot', operation)


if __name__ == '__main__':
    unittest.main()
