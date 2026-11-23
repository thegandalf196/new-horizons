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
        Track=[PSCustomObject]@{Enabled=$true;SelectedIndex=0};Notes=[PSCustomObject]@{Enabled=$true};
        State=[PSCustomObject]@{Runtime=$runtime;Offer=$null;Busy=$false;ExitCode=0;Track='stable';Own='stable';
            Runtimes=@{stable=$runtime;beta=$null};Code=(Join-Path $env:SOURCE 'tools/windows');Notes=@{};
            Checking=$false;PlayRequested=$false;CloseRequested=$false;Launches=0}}
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

    def test_beta_check_offers_only_the_newest_published_prerelease(self):
        self.execute(r'''
$metadata=Join-Path $env:FIXTURE 'metadata.json'
$script:Downloads=@()
function Invoke-UpdateDownload($URL,$Output,$MaximumBytes){$script:Downloads+=$URL;[IO.File]::WriteAllText($Output,$script:ReleaseText)}
# A beta needs a -beta.N or -rc.N version and the prerelease flag; either alone is ignored.
$missing=New-FixtureRelease '0.94.0-beta.4' $true $false
$missing.Remove('prerelease')
$list=@((New-FixtureRelease '0.94.0' $false $false),(New-FixtureRelease '0.94.0-beta.3' $false $false),
    (New-FixtureRelease '0.94.0-beta.2' $true $true),$missing,(New-FixtureRelease '0.93.9' $true $false),
    (New-FixtureRelease '0.94.0-beta.1' $true $false),@{tag_name='nonsense'},'not a release')
$script:ReleaseText=ConvertTo-Json -InputObject $list -Depth 6
$offer=Get-NHLatestPlayerOffer $metadata 'beta'
Require ($script:Downloads[0] -ceq $script:ReleaseListAPI) 'Beta did not list recent releases'
Require ($offer.version -ceq '0.94.0-beta.1' -and $offer.channel -ceq 'beta' -and $offer.format -eq 2) 'Wrong beta offer'
Require (-not [IO.File]::Exists($metadata+'.offer')) 'Validation scratch survived'
foreach ($release in @((New-FixtureRelease '0.94.0-beta.1' $true $false),(New-FixtureRelease '0.94.0-rc.1' $true $false),
                       (New-FixtureRelease '0.94.0-rc.1' $false $false))) {
    $script:ReleaseText=$release|ConvertTo-Json -Depth 6
    $rejected=$false
    try{$null=Get-NHLatestPlayerOffer $metadata}catch{$rejected=$true}
    Require $rejected ('Stable accepted ' + $release.tag_name)
}
$script:ReleaseText=ConvertTo-Json -InputObject @((New-FixtureRelease '0.92.3-beta.1' $true $false)) -Depth 6
$view=New-FixtureView
$view.State.Track='beta'
$view.State.Runtime=$null
Complete-NHPlayerCheck $view (Get-NHLatestPlayerOffer $metadata 'beta')
Require ($null -eq $view.State.Offer) 'A beta not newer than stable was offered'
Require ($view.Status.Text -ceq 'No beta newer than your installed versions is available right now.') 'Wrong empty beta status'
$script:ReleaseText=ConvertTo-Json -InputObject @((New-FixtureRelease '0.94.0' $false $false)) -Depth 6
Require ($null -eq (Get-NHLatestPlayerOffer $metadata 'beta')) 'A stable release was offered as a beta'
$script:ReleaseText='[]'
Require ($null -eq (Get-NHLatestPlayerOffer $metadata 'beta')) 'Empty release list produced an offer'
Complete-NHPlayerCheck $view $offer
Require ($view.State.Offer.version -ceq '0.94.0-beta.1' -and $view.Status.Text -ceq '0.94.0 Beta 1 is available.') 'Beta offer hidden'
# The Beta track also offers a release candidate, named for players.
$script:ReleaseText=ConvertTo-Json -InputObject @((New-FixtureRelease '0.94.0-rc.1' $true $false),(New-FixtureRelease '0.94.0-beta.10' $true $false)) -Depth 6
Complete-NHPlayerCheck $view (Get-NHLatestPlayerOffer $metadata 'beta')
Require ($view.Status.Text -ceq '0.94.0 Release Candidate 1 is available.') ('Wrong rc status: ' + $view.Status.Text)
''')

    def test_version_labels_name_betas_and_release_candidates(self):
        self.execute(r'''
foreach ($case in @(@('0.94.0-beta.3','0.94.0 Beta 3'),@('0.94.0-rc.1','0.94.0 Release Candidate 1'),@('0.94.0','0.94.0'),
                    @('0.94.0-gamma.1','0.94.0-gamma.1'))) {
    Require ((Format-NHVersionLabel $case[0]) -ceq $case[1]) ('Wrong label for ' + $case[0])
}
Require ((Get-NHBetaName '0.94.0-rc.2') -ceq '0.94.0 Release Candidate 2' -and (Get-NHBetaName '0.93.1') -ceq 'Beta 0.93.1') 'Wrong beta names'
$view=New-FixtureView
$view.State.Track='beta'
[IO.File]::WriteAllText((Join-Path $runtime 'config/newHorizonsVersion.json'),'{"version":"0.94.0-rc.1"}')
$view.State.Runtimes['beta']=$runtime
Update-NHPlayerVersion $view
Require ($view.Version.Text -ceq ('0.94.0 Release Candidate 1 ' + [char]0x00B7 + ' separate saves')) ('Wrong version line: ' + $view.Version.Text)
''')

    def test_promoted_stable_is_never_offered_to_a_beta_player(self):
        self.execute(r'''
# 0.94.0-beta.5 was promoted to Stable 0.94.0; the beta player waits for 0.95.0-beta.1.
$metadata=Join-Path $env:FIXTURE 'metadata.json'
function Invoke-UpdateDownload($URL,$Output,$MaximumBytes){[IO.File]::WriteAllText($Output,$script:ReleaseText)}
function New-FixtureRuntime([string]$Name,[string]$Version) {
    $path=Join-Path $env:FIXTURE $Name
    $null=[IO.Directory]::CreateDirectory((Join-Path $path 'config'))
    [IO.File]::WriteAllText((Join-Path $path 'config/newHorizonsVersion.json'),('{"version":"'+$Version+'"}'))
    return $path
}
$published=@((New-FixtureRelease '0.94.0' $false $false),(New-FixtureRelease '0.94.0-beta.5' $true $false),
    (New-FixtureRelease '0.94.0-beta.4' $true $false),(New-FixtureRelease '0.92.8' $false $false))
$script:ReleaseText=ConvertTo-Json -InputObject $published -Depth 6
$view=New-FixtureView
$view.State.Track='beta'
$view.State.Runtimes['beta']=New-FixtureRuntime 'beta-5' '0.94.0-beta.5'
$view.State.Runtime=$view.State.Runtimes['beta']
Complete-NHPlayerCheck $view (Get-NHLatestPlayerOffer $metadata 'beta')
Require ($null -eq $view.State.Offer) 'The promoted stable or an older beta was offered'
Require ($view.Status.Text -ceq 'No beta newer than your installed versions is available right now.') 'Wrong waiting status'
$script:ReleaseText=ConvertTo-Json -InputObject (@((New-FixtureRelease '0.95.0-beta.1' $true $false)) + $published) -Depth 6
Complete-NHPlayerCheck $view (Get-NHLatestPlayerOffer $metadata 'beta')
Require ($view.State.Offer.version -ceq '0.95.0-beta.1' -and $view.State.Offer.channel -ceq 'beta') 'The next beta was not offered'
# A 0.93.x beta with this launcher moves to the first suffixed beta: 0.93.2 < 0.94.0-beta.1.
$view.State.Offer=$null
$view.State.Runtimes['beta']=New-FixtureRuntime 'beta-0932' '0.93.2'
$script:ReleaseText=ConvertTo-Json -InputObject @((New-FixtureRelease '0.94.0-beta.1' $true $false),(New-FixtureRelease '0.93.1' $true $false)) -Depth 6
Complete-NHPlayerCheck $view (Get-NHLatestPlayerOffer $metadata 'beta')
Require ($view.State.Offer.version -ceq '0.94.0-beta.1') 'No upgrade from 0.93.2 to the first suffixed beta'
# An empty beta track still needs a beta newer than the installed stable (0.92.3).
$view.State.Offer=$null
$view.State.Runtimes['beta']=$null
$view.State.Runtime=$null
$script:ReleaseText=ConvertTo-Json -InputObject @((New-FixtureRelease '0.92.3-beta.1' $true $false),(New-FixtureRelease '0.94.0' $false $false)) -Depth 6
Complete-NHPlayerCheck $view (Get-NHLatestPlayerOffer $metadata 'beta')
Require ($null -eq $view.State.Offer) 'An empty beta took a stable or an older beta'
# The Stable track still takes the promoted release.
$script:ReleaseText=(New-FixtureRelease '0.94.0' $false $false)|ConvertTo-Json -Depth 6
Require ((Get-NHLatestPlayerOffer $metadata).version -ceq '0.94.0') 'Stable lost the promoted release'
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
    $p=[PSCustomObject]@{Id=4242}
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

    def test_beta_package_opens_on_beta_and_offers_an_older_stable_on_its_empty_stable_track(self):
        self.execute(r'''
$view=New-FixtureView
$view.State.Own='beta'
$view.State.Track='beta'
$view.State.Runtimes=@{stable=$null;beta=$runtime}
Update-NHPlayerVersion $view
Require ($view.Version.Text -ceq ('Beta 0.92.3 ' + [char]0x00B7 + ' separate saves')) 'Beta package not shown as beta'
$script:Checks=0
function Invoke-NHPlayerUpdate($View,$Original,$Install){ $script:Checks++ }
Complete-NHPlayerCheck $view ([PSCustomObject]@{version='0.92.2'})
Require ($null -eq $view.State.Offer) 'An older beta was offered'
Set-NHPlayerTrack $view $runtime 0
Require ($view.State.Track -ceq 'stable' -and $null -eq $view.State.Runtime -and -not $view.Play.Enabled) 'Empty stable playable'
Require ($script:Checks -eq 1) 'Empty stable did not check'
Require ($view.Version.Text -ceq 'No stable version installed yet. Check for updates to find one.') 'Wrong empty stable label'
Require ((Get-NHTrackBaseline $view) -eq [Version]'0.0.0') 'Empty stable kept a floor'
Complete-NHPlayerCheck $view $null
Require ($view.Status.Text -ceq 'No stable release is available right now.') 'Wrong empty stable status'
Complete-NHPlayerCheck $view ([PSCustomObject]@{version='0.92.1'})
Require ($view.State.Offer.version -ceq '0.92.1' -and $view.Status.Text.StartsWith('Version 0.92.1 is available.')) 'Older stable not offered'
$script:Calls=@()
function New-NHPlayerProcess($Runtime,$Script,$Arguments,$LocalData){
    $script:Calls+=[PSCustomObject]@{Runtime=$Runtime;Arguments=(@($Arguments) -join '|');LocalData=$LocalData}
    $p=[PSCustomObject]@{Id=4242}
    $p|Add-Member ScriptMethod Dispose {}
    return $p
}
function Wait-NHPlayerProcess { return 0 }
Invoke-NHPlayerGame $view $false
Require ($view.Status.Text.Contains('No stable version is installed.') -and $script:Calls.Count -eq 0) 'Empty stable played the beta'
Invoke-NHPlayerGame $view $true
Require ($script:Calls[0].Runtime -ceq $runtime -and $script:Calls[0].LocalData -ceq '') 'Empty stable chooser'
$view.State.Track='beta'
$view.State.Runtime=$runtime
Invoke-NHPlayerGame $view $false
Require ($script:Calls[1].LocalData -ceq (Join-Path ([IO.Path]::GetFullPath($env:LOCALAPPDATA)) 'NewHorizons-Beta')) 'Beta package used the stable profile'
''')

    def test_automatic_check_preference_is_read_from_the_opening_track(self):
        self.execute(r'''
$local=[IO.Path]::GetFullPath($env:LOCALAPPDATA)
$config=Join-Path $local 'NewHorizons-Beta\HeroesIII-NewHorizons\config'
$null=[IO.Directory]::CreateDirectory($config)
[IO.File]::WriteAllText((Join-Path $config 'settings.json'),'{"general":{"updateChecks":false}}')
Require (Test-NHLauncherAutoCheck 'stable') 'Stable read the beta preference'
Require (-not (Test-NHLauncherAutoCheck 'beta')) 'Beta ignored its own preference'
''')

    def test_first_beta_start_copies_the_stable_saves_once_and_never_changes_them(self):
        self.execute(r'''
$local=[IO.Path]::GetFullPath($env:LOCALAPPDATA)
$saves=Join-Path $local 'new-horizons\Saves'
$null=[IO.Directory]::CreateDirectory((Join-Path $saves 'campaign'))
[IO.File]::WriteAllText((Join-Path $saves 'first.vsgm1'),'save one')
[IO.File]::WriteAllText((Join-Path $saves 'campaign\second.vsgm1'),'save two')
$beta=Join-Path $local 'NewHorizons-Beta'
Copy-NHStableSavesToBeta $local
Copy-NHStableSavesToBeta (Join-Path $local 'Other')
Require (-not [IO.Directory]::Exists($beta) -and -not [IO.Directory]::Exists((Join-Path $local 'Other'))) 'A stable root was seeded'
Copy-NHStableSavesToBeta $beta
$copied=Join-Path $beta 'new-horizons\Saves'
Require ([IO.File]::ReadAllText((Join-Path $copied 'first.vsgm1')) -ceq 'save one') 'Save not copied'
Require ([IO.File]::ReadAllText((Join-Path $copied 'campaign\second.vsgm1')) -ceq 'save two') 'Nested save not copied'
Require ([IO.File]::ReadAllText((Join-Path $saves 'first.vsgm1')) -ceq 'save one') 'Stable save changed'
Require (@(Get-ChildItem -LiteralPath $local -Filter 'NewHorizons-Beta.copying-*').Count -eq 0) 'Staging copy left behind'
[IO.File]::WriteAllText((Join-Path $saves 'first.vsgm1'),'changed in stable')
Copy-NHStableSavesToBeta $beta
Require ([IO.File]::ReadAllText((Join-Path $copied 'first.vsgm1')) -ceq 'save one') 'An existing beta root was reseeded'
$other=Join-Path $env:FIXTURE 'other-local'
$null=[IO.Directory]::CreateDirectory($other)
Copy-NHStableSavesToBeta (Join-Path $other 'NewHorizons-Beta')
Require (@(Get-ChildItem -LiteralPath $other).Count -eq 0) 'Beta root created without stable saves'
''')

    def test_failed_save_copy_leaves_no_beta_root_and_stops_the_start(self):
        if os.name == 'nt':
            self.skipTest('The unreadable fixture uses POSIX permissions')
        self.execute(r'''
$local=[IO.Path]::GetFullPath($env:LOCALAPPDATA)
$saves=Join-Path $local 'new-horizons\Saves'
$null=[IO.Directory]::CreateDirectory($saves)
$save=Join-Path $saves 'first.vsgm1'
[IO.File]::WriteAllText($save,'save one')
[IO.File]::SetUnixFileMode($save, [IO.UnixFileMode]::None)
$message=''
try { Copy-NHStableSavesToBeta (Join-Path $local 'NewHorizons-Beta') } catch { $message=$_.Exception.Message }
[IO.File]::SetUnixFileMode($save, [IO.UnixFileMode]'UserRead,UserWrite')
Require ($message.StartsWith('Could not copy your saves to the beta') -and $message.Contains('Your saves are unchanged.')) ('Wrong failure: ' + $message)
Require (-not [IO.Directory]::Exists((Join-Path $local 'NewHorizons-Beta'))) 'A partial beta root remained'
Require (@(Get-ChildItem -LiteralPath $local -Filter 'NewHorizons-Beta.copying-*').Count -eq 0) 'Staging copy left behind'
''')

    def test_game_start_seeds_the_beta_root_before_creating_any_profile(self):
        source = (ROOT / 'tools/windows/Start-New-Horizons.ps1').read_text()
        self.assertIn(". (Join-Path $PSScriptRoot 'Track-Data.ps1')", source)
        seed = source.index('Copy-NHStableSavesToBeta $localData')
        self.assertLess(source.index("$localData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process')"), seed)
        self.assertLess(seed, source.index('$null = [IO.Directory]::CreateDirectory($profileRoot)'))
        # A beta package's own command-line and setup starts use the beta root, after the
        # update store (keyed by the real LOCALAPPDATA) chose the runtime.
        select = source.index('$selectedRelease = Select-NHPortableRelease $packageRoot $false')
        route = source.index("if ((Get-NHPackageChannel $packageRoot) -ceq 'beta') { $env:LOCALAPPDATA = Get-NHTrackLocalData 'beta' }")
        self.assertLess(select, route)
        self.assertLess(route, source.index("& (Join-Path $selectedRelease.runtime 'Start-New-Horizons.ps1') @PSBoundParameters"))

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
    $p=[PSCustomObject]@{Disposed=$false;Id=4242}
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
    $p=[PSCustomObject]@{Id=4242}
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
    $p=[PSCustomObject]@{Id=4242}
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
