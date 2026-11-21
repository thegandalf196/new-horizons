# SPDX-License-Identifier: GPL-2.0-or-later
# Ordinary Windows EXE entry: presentation and exact owned child supervision.
# Setup, all CLI arguments and crash reporting retain Start-New-Horizons routes.
. (Join-Path $PSScriptRoot 'Player-Launcher-Notes.ps1')

function New-NHPlayerProcess([string]$Runtime, [string]$Script, [string[]]$Arguments = @(), [string]$LocalData = '') {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = Join-Path $PSHOME 'powershell.exe'
    $info.WorkingDirectory = $Runtime
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $all = @('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$Script) + $Arguments
    $info.Arguments = (@($all | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    if ($info.Arguments.Length -ge 32000) { throw 'Launcher paths exceed the Windows command limit.' }
    # Per-child values do not leak to later setup or command-line invocations.
    $info.EnvironmentVariables['NH_WINDOWS_PORTABLE_UPDATE_CHILD'] = $Runtime
    $info.EnvironmentVariables['NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1'] = '{"version":1,"arguments":[]}'
    $info.EnvironmentVariables['NH_LAUNCHER_MANAGES_UPDATES'] = '1'
    $info.EnvironmentVariables.Remove('NH_PORTABLE_UPDATE_REQUEST')
    $info.EnvironmentVariables.Remove('NH_LAUNCHER_READY_EVENT')
    # dirs.json expands %LOCALAPPDATA%; a track root moves the whole profile and saves.
    if ($LocalData) { $info.EnvironmentVariables['LOCALAPPDATA'] = $LocalData }
    return [Diagnostics.Process]::Start($info)
}

function Wait-NHPlayerProcess($Process, $View, [string]$StatusPath, [int]$TimeoutSeconds = 0) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        while (-not $Process.WaitForExit(100)) {
            if ($TimeoutSeconds -gt 0 -and $clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                # Only metadata-check children use this deadline. Installation
                # and the real game retain their complete owned wait lifecycle.
                try { if (-not $Process.HasExited) { $Process.Kill() } }
                catch { if (-not $Process.HasExited) { throw } }
                throw 'Update check timed out. You can still play the installed game.'
            }
            if ($StatusPath -and [IO.File]::Exists($StatusPath)) {
                try {
                    $file = New-Object IO.FileInfo($StatusPath)
                    if ($file.Length -le 1024) { $View.Status.Text = [IO.File]::ReadAllText($StatusPath) }
                } catch { } # A progress write can race a read; completion is authoritative.
            }
            [Windows.Forms.Application]::DoEvents()
        }
    } finally {
        # Presentation failure must not abandon an exact owned child or its lock.
        $Process.WaitForExit()
    }
    return [int]$Process.ExitCode
}

function Set-NHPlayerBusy($View, [bool]$Busy) {
    $View.State.Busy = $Busy
    foreach ($control in @($View.Check,$View.Assets,$View.Track,$View.Notes)) { $control.Enabled = -not $Busy }
    $View.Play.Enabled = -not $Busy -and $null -ne $View.State.Runtime
    $View.Install.Enabled = -not $Busy -and $null -ne $View.State.Offer
    $View.Bar.Visible = $Busy
}

function Get-NHTrackLocalData([string]$Track) {
    # Beta keeps its own profile, saves, settings, cache and logs below this root.
    if ($Track -ceq 'stable') { return '' }
    if ($Track -cne 'beta') { throw 'Unknown release track.' }
    $local = [Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process')
    if ([string]::IsNullOrWhiteSpace($local) -or -not [IO.Path]::IsPathRooted($local)) {
        throw 'LOCALAPPDATA is unavailable. Run as your normal Windows user.'
    }
    $root = Join-Path ([IO.Path]::GetFullPath($local)) 'NewHorizons-Beta'
    Assert-PlainPath $root
    return $root
}

function Get-NHBetaAssetArguments([string]$BetaLocalData) {
    # Beta reuses the stable verified copy of the original game files until the
    # player imports or selects its own. Setup verifies the copy as usual.
    $own = Join-Path $BetaLocalData 'HeroesIII-NewHorizons\content\.nh-assets.json'
    if ([IO.File]::Exists($own)) { return @() }
    $local = [IO.Path]::GetFullPath([Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process'))
    $stable = Join-Path $local 'HeroesIII-NewHorizons\content'
    if (-not [IO.File]::Exists((Join-Path $stable '.nh-assets.json'))) { return @() }
    return @('-AssetsPath', $stable)
}

function Update-NHPlayerVersion($View) {
    $runtime = $View.State.Runtime
    if ($View.State.Track -ceq 'stable') {
        $View.Version.Text = 'Installed version: ' + (Get-NHProductVersion $runtime).ToString()
    } elseif ($null -eq $runtime) {
        $View.Version.Text = 'No beta installed yet. Check for updates to find one.'
    } else {
        $View.Version.Text = 'Beta ' + (Get-NHProductVersion $runtime).ToString() + ' ' + [char]0x00B7 + ' separate saves'
    }
}

function Get-NHTrackBaseline($View) {
    # A beta is offered only when newer than both installed tracks.
    $baseline = Get-NHProductVersion $View.State.Runtimes['stable']
    $beta = $View.State.Runtimes['beta']
    if ($View.State.Track -ceq 'beta' -and $null -ne $beta) {
        $betaVersion = Get-NHProductVersion $beta
        if ($betaVersion -gt $baseline) { $baseline = $betaVersion }
    }
    return $baseline
}

function Set-NHPlayerTrack($View, [string]$Original, [int]$Index) {
    $track = 'stable'
    if ($Index -eq 1) { $track = 'beta' }
    if ($View.State.Busy) {
        if ($track -cne $View.State.Track) { $View.Track.SelectedIndex = [int]($View.State.Track -ceq 'beta') }
        return
    }
    if ($track -ceq $View.State.Track) { return }
    $View.State.Track = $track
    $View.State.Runtime = $View.State.Runtimes[$track]
    $View.State.Offer = $null
    Update-NHPlayerVersion $View
    Set-NHPlayerBusy $View $false
    if ($track -ceq 'beta') {
        $View.Status.Text = 'Beta builds try experimental changes and keep their own saves and settings.'
        if ($null -eq $View.State.Runtime) { Invoke-NHPlayerUpdate $View $Original $false }
    } else {
        $View.Status.Text = 'Ready to play.'
    }
}

function Complete-NHPlayerCheck($View, $Offer) {
    $beta = $View.State.Track -ceq 'beta'
    if ($null -ne $Offer -and [Version]$Offer.version -gt (Get-NHTrackBaseline $View)) {
        $View.State.Offer = $Offer
        if ($beta) {
            $View.Status.Text = 'Beta ' + $Offer.version + ' is available.'
        } else {
            $View.Status.Text = 'Version ' + $Offer.version + ' is available. Select Install update to accept.'
        }
    } elseif ($beta) {
        $View.Status.Text = 'No beta newer than your installed versions is available right now.'
    } else { $View.Status.Text = 'You have the latest stable release.' }
}

function Complete-NHPlayerInstall($View, [string]$Original) {
    # Re-read both selections; installing one track never changes the other.
    $View.State.Runtimes['stable'] = (Select-NHPortableRelease $Original $false).runtime
    $View.State.Runtimes['beta'] = Get-NHTrackRuntime $Original 'beta'
    $View.State.Runtime = $View.State.Runtimes[$View.State.Track]
    Update-NHPlayerVersion $View
    $View.Status.Text = 'Update installed. Select Play to start.'
}

function Invoke-NHPlayerUpdate($View, [string]$Original, [bool]$Install) {
    if ($View.State.Busy) { return }
    if ($Install -and $null -eq $View.State.Offer) { return }
    $directory = Join-Path ([IO.Path]::GetTempPath()) ('nh-launcher-' + [Guid]::NewGuid().ToString('N'))
    $process = $null
    Set-NHPlayerBusy $View $true
    try {
        Assert-PlainPath $directory
        $null = [IO.Directory]::CreateDirectory($directory)
        $output = Join-Path $directory 'result.json'
        $status = Join-Path $directory 'status.txt'
        $request = Join-Path $directory 'accepted.json'
        $notes = Join-Path $directory 'notes.json'
        $operation = if ($Install) { 'Install' } else { 'Check' }
        $View.Status.Text = 'Checking for updates...'
        if ($Install) {
            $View.Status.Text = 'Downloading and verifying accepted update...'
        } elseif ($View.State.Track -ceq 'beta') {
            $View.Status.Text = 'Checking for a beta release...'
        }
        if ($Install) { [IO.File]::WriteAllText($request, ($View.State.Offer | ConvertTo-Json -Compress)) }
        $arguments = @('-Operation',$operation,'-Original',$Original,'-Output',$output,'-StatusPath',$status)
        if ($Install) { $arguments += @('-Request',$request) }
        else { $arguments += @('-Channel',$View.State.Track) + (Get-NHPlayerNotesArguments $View $notes) }
        # A handed-over launcher runs its own background operations.
        $script = Join-Path $View.State.Code 'Player-Launcher-Operation.ps1'
        Assert-PlainPath $script
        $process = New-NHPlayerProcess $Original $script $arguments
        $timeout = if ($Install) { 0 } else { 5 }
        $code = Wait-NHPlayerProcess $process $View $status $timeout
        $result = Read-NHSmallUpdateJson $output
        if ($code -ne 0 -or -not $result.ok) { throw $result.error }
        $View.State.Offer = $null
        if ($Install) { Complete-NHPlayerInstall $View $Original } else {
            Complete-NHPlayerCheck $View $result.value
            $View.State.Notes[$View.State.Track] = Read-NHReleaseNotesFile $notes
        }
    } catch {
        $View.Status.Text = 'Update unavailable: ' + $_.Exception.Message + ' Your installed game is unchanged.'
    } finally {
        if ($null -ne $process) { $process.Dispose() }
        if ([IO.Directory]::Exists($directory)) { [IO.Directory]::Delete($directory, $true) }
        Set-NHPlayerBusy $View $false
    }
}

function Invoke-NHPlayerGame($View, [bool]$Setup) {
    if ($View.State.Busy) { return }
    $process = $null
    Set-NHPlayerBusy $View $true
    try {
        $View.Status.Text = if ($Setup) { 'Selecting original game files...' } else { 'Preparing game artwork and starting New Horizons...' }
        $arguments = @()
        if ($Setup) { $arguments = @('-SelectAssets','-SetupOnly') }
        $runtime = $View.State.Runtime
        # An empty beta track never plays the original; it can still choose game files.
        if ($null -eq $runtime) {
            if (-not $Setup) { throw 'No beta is installed.' }
            $runtime = $View.State.Runtimes['stable']
        }
        $localData = Get-NHTrackLocalData $View.State.Track
        if ($localData -and -not $Setup) { $arguments = @(Get-NHBetaAssetArguments $localData) }
        $script = Join-Path $runtime 'Start-New-Horizons.ps1'
        Assert-PlainPath $script
        $process = New-NHPlayerProcess $runtime $script $arguments $localData
        $View.Form.Hide()
        $code = Wait-NHPlayerProcess $process $View $null
        $View.State.ExitCode = $code
        $View.Status.Text = if ($code -eq 0) { 'Ready to play.' } else { 'New Horizons exited with status ' + $code + '.' }
    } catch {
        $View.Status.Text = 'Could not start: ' + $_.Exception.Message
        $View.State.ExitCode = 1
    } finally {
        if ($null -ne $process) { $process.Dispose() }
        $View.Form.Show()
        Set-NHPlayerBusy $View $false
    }
}

function New-NHPlayerButton($Form, [string]$Text, [int]$X, [int]$Y, [int]$Width) {
    $button = New-Object Windows.Forms.Button
    $button.Text = $Text
    $button.Location = New-Object Drawing.Point($X,$Y)
    $button.Size = New-Object Drawing.Size($Width,32)
    $Form.Controls.Add($button)
    return $button
}

function New-NHPlayerTrack($Form) {
    $label = New-Object Windows.Forms.Label
    $label.Location = New-Object Drawing.Point(18,21)
    $label.Size = New-Object Drawing.Size(100,20)
    $label.Text = 'Version track'
    $Form.Controls.Add($label)
    $track = New-Object Windows.Forms.ComboBox
    $track.DropDownStyle = 'DropDownList'
    $track.Location = New-Object Drawing.Point(124,18)
    $track.Size = New-Object Drawing.Size(354,24)
    $null = $track.Items.Add('Stable')
    $null = $track.Items.Add('Beta (experimental, separate saves)')
    $track.SelectedIndex = 0
    $Form.Controls.Add($track)
    return $track
}

function New-NHPlayerView($Runtimes, [string]$CodeRoot) {
    Add-Type -AssemblyName System.Windows.Forms
    $form = New-Object Windows.Forms.Form
    $form.Text = 'New Horizons'
    $form.ClientSize = New-Object Drawing.Size(500,276)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $version = New-Object Windows.Forms.Label
    $version.Location = New-Object Drawing.Point(18,54)
    $version.Size = New-Object Drawing.Size(340,24)
    $form.Controls.Add($version)
    $status = New-Object Windows.Forms.Label
    $status.Location = New-Object Drawing.Point(18,174)
    $status.Size = New-Object Drawing.Size(460,52)
    $status.Text = 'Ready to play.'
    $form.Controls.Add($status)
    $bar = New-Object Windows.Forms.ProgressBar
    $bar.Location = New-Object Drawing.Point(18,239)
    $bar.Size = New-Object Drawing.Size(460,18)
    $bar.Style = 'Marquee'
    $bar.MarqueeAnimationSpeed = 30
    $bar.Visible = $false
    $form.Controls.Add($bar)
    $view = [PSCustomObject]@{Form=$form;Version=$version;Status=$status;Bar=$bar;Track=(New-NHPlayerTrack $form);
        Play=(New-NHPlayerButton $form 'Play' 18 90 460);
        Check=(New-NHPlayerButton $form 'Check for updates' 18 132 145);
        Install=(New-NHPlayerButton $form 'Install update' 173 132 135);
        Assets=(New-NHPlayerButton $form 'Original game files' 318 132 160);
        Notes=(New-NHPlayerButton $form "What's new" 368 50 110);
        State=[PSCustomObject]@{Runtime=$Runtimes['stable'];Offer=$null;Busy=$false;ExitCode=0;
            Track='stable';Runtimes=$Runtimes;Code=$CodeRoot;Notes=@{}}}
    $view.Install.Enabled = $false
    Update-NHPlayerVersion $view
    return $view
}

function Test-NHLauncherAutoCheck {
    try {
        $settings = Join-Path $env:LOCALAPPDATA 'HeroesIII-NewHorizons\config\settings.json'
        if ([IO.File]::Exists($settings)) {
            Assert-PlainPath $settings
            $info = New-Object IO.FileInfo($settings)
            if ($info.Length -le 1048576) {
                $value = [IO.File]::ReadAllText($settings) | ConvertFrom-Json
                $general = $value.PSObject.Properties['general']
                if ($null -ne $general -and $null -ne $general.Value) {
                    $checks = $general.Value.PSObject.Properties['updateChecks']
                    if ($null -ne $checks -and $checks.Value -is [bool] -and -not $checks.Value) { return $false }
                }
            }
        }
    } catch { } # Optional preference failure does not hide manual update controls.
    return $true
}

function Show-NHPlayerLauncher([string]$Original, [string]$CodeRoot = '') {
    # CodeRoot is this launcher's own package; it differs from Original after a hand-over.
    if (-not $CodeRoot) { $CodeRoot = $Original }
    $runtimes = @{stable=(Select-NHPortableRelease $Original $false).runtime; beta=(Get-NHTrackRuntime $Original 'beta')}
    $view = New-NHPlayerView $runtimes $CodeRoot
    try {
        $view.Form.add_FormClosing({ param($sender,$event) if ($view.State.Busy) { $event.Cancel=$true } })
        $view.Play.add_Click({ Invoke-NHPlayerGame $view $false })
        $view.Assets.add_Click({ Invoke-NHPlayerGame $view $true })
        $view.Check.add_Click({ Invoke-NHPlayerUpdate $view $Original $false })
        $view.Install.add_Click({ Invoke-NHPlayerUpdate $view $Original $true })
        $view.Notes.add_Click({ Show-NHPlayerNotes $view $Original })
        $view.Track.add_SelectedIndexChanged({ Set-NHPlayerTrack $view $Original $view.Track.SelectedIndex })
        $view.Form.add_Shown({ if (Test-NHLauncherAutoCheck) { Invoke-NHPlayerUpdate $view $Original $false } })
        $null = $view.Form.ShowDialog()
        return [int]$view.State.ExitCode
    } finally { $view.Form.Dispose() }
}
