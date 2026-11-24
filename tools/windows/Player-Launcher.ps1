# SPDX-License-Identifier: GPL-2.0-or-later
# Ordinary Windows EXE entry: presentation and exact owned child supervision.
# Setup, all CLI arguments and crash reporting retain Start-New-Horizons routes.
. (Join-Path $PSScriptRoot 'Setup-Progress.ps1')
. (Join-Path $PSScriptRoot 'Player-Launcher-Notes.ps1')
. (Join-Path $PSScriptRoot 'Player-Launcher-Log.ps1')
. (Join-Path $PSScriptRoot 'Track-Data.ps1')
# Covers a cold PowerShell start, antivirus scans and one GitHub request that has
# its own 20-second bound. Play never waits for it.
$script:NHCheckDeadlineSeconds = 45

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
    $info.EnvironmentVariables.Remove('NH_LAUNCHER_LOG_SESSION')
    # dirs.json expands %LOCALAPPDATA%; a track root moves the whole profile and saves.
    if ($LocalData) { $info.EnvironmentVariables['LOCALAPPDATA'] = $LocalData }
    return [Diagnostics.Process]::Start($info)
}

function Update-NHPlayerStatus($View, [string]$StatusPath, [string]$Phase) {
    # Shows the child's progress; returns the update-check phase it last reported.
    if (-not $StatusPath -or -not [IO.File]::Exists($StatusPath)) { return $Phase }
    try {
        if ((New-Object IO.FileInfo($StatusPath)).Length -gt 1024) { return $Phase }
        $text = [IO.File]::ReadAllText($StatusPath)
        if ($text.StartsWith('phase:')) {
            $Phase = $text.Substring(6)
            $text = Get-NHCheckPhaseText $Phase 0
        }
        if ($text) { $View.Status.Text = $text }
    } catch { } # A progress write can race a read; completion is authoritative.
    return $Phase
}

function Stop-NHPlayerChild($Process) {
    try { if (-not $Process.HasExited) { $Process.Kill() } }
    catch { if (-not $Process.HasExited) { throw } }
}

function Wait-NHPlayerProcess($Process, $View, [string]$StatusPath, [int]$TimeoutSeconds = 0) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $phase = 'launch'
    try {
        while (-not $Process.WaitForExit(100)) {
            $phase = Update-NHPlayerStatus $View $StatusPath $phase
            # Only metadata-check children have a deadline, and Play or closing stops them.
            # Installation and the real game retain their complete owned wait lifecycle.
            if ($TimeoutSeconds -gt 0 -and ($View.State.PlayRequested -or $View.State.CloseRequested)) {
                Stop-NHPlayerChild $Process
                throw (New-Object OperationCanceledException 'The player stopped the update check.')
            }
            if ($TimeoutSeconds -gt 0 -and $clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                Stop-NHPlayerChild $Process
                Write-NHLauncherLog ('Check deadline of ' + $TimeoutSeconds + ' s reached after ' +
                    [int]$clock.Elapsed.TotalMilliseconds + ' ms; the child was in phase ' + $phase + ' and was stopped')
                throw (New-Object TimeoutException ('Update check timed out while ' + (Get-NHCheckPhaseText $phase 1) +
                    ' after ' + $TimeoutSeconds + ' s. You can still play the installed game.'))
            }
            [Windows.Forms.Application]::DoEvents()
        }
    } finally {
        # Presentation failure must not abandon an exact owned child or its lock.
        $Process.WaitForExit()
    }
    return [int]$Process.ExitCode
}

function Set-NHPlayerBusy($View, [bool]$Busy, [bool]$Checking = $false) {
    $View.State.Busy = $Busy
    $View.State.Checking = $Busy -and $Checking
    foreach ($control in @($View.Check,$View.Assets,$View.Track,$View.Notes)) { $control.Enabled = -not $Busy }
    # Play stays available during an update check; selecting it stops the check.
    $View.Play.Enabled = (-not $Busy -or $View.State.Checking) -and -not $View.State.PlayRequested -and
        $null -ne $View.State.Runtime
    $View.Install.Enabled = -not $Busy -and $null -ne $View.State.Offer
    $View.Bar.Visible = $Busy
}

function Update-NHPlayerVersion($View) {
    $runtime = $View.State.Runtime
    if ($null -eq $runtime) {
        $name = if ($View.State.Track -ceq 'beta') { 'beta' } else { 'stable version' }
        $View.Version.Text = 'No ' + $name + ' installed yet. Check for updates to find one.'
    } elseif ($View.State.Track -ceq 'stable') {
        $View.Version.Text = 'Installed version: ' + (Format-NHVersionLabel (Get-NHProductVersionText $runtime))
    } else {
        $View.Version.Text = (Get-NHBetaName (Get-NHProductVersionText $runtime)) + ' ' + [char]0x00B7 + ' separate saves'
    }
    # As on Linux, the beta track's version line is blue.
    if ($View.Version.PSObject.Properties['ForeColor']) {
        $View.Version.ForeColor = Get-NHLookColor $(if ($View.State.Track -ceq 'beta') { 'Beta' } else { 'Version' })
    }
}

function Get-NHTrackBaseline($View) {
    # Stable is offered when newer than the installed stable, or any stable when
    # none is installed (a beta package); a beta only when newer than both tracks.
    $baseline = [Version]'0.0.0'
    $tracks = @('stable')
    if ($View.State.Track -ceq 'beta') { $tracks += 'beta' }
    foreach ($track in $tracks) {
        $runtime = $View.State.Runtimes[$track]
        if ($null -eq $runtime) { continue }
        $version = Get-NHProductVersion $runtime
        if ($version -gt $baseline) { $baseline = $version }
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
    Write-NHLauncherLog ('Track changed to ' + $track)
    $View.State.Track = $track
    $View.State.Runtime = $View.State.Runtimes[$track]
    $View.State.Offer = $null
    Update-NHPlayerVersion $View
    Set-NHPlayerBusy $View $false
    if ($track -ceq 'beta') {
        $View.Status.Text = 'Beta builds try experimental changes and keep their own saves and settings.'
    } else {
        $View.Status.Text = 'Ready to play.'
    }
    # An empty track looks for its first release.
    if ($null -eq $View.State.Runtime) { Invoke-NHPlayerUpdate $View $Original $false }
}

function Complete-NHPlayerCheck($View, $Offer) {
    $beta = $View.State.Track -ceq 'beta'
    if ($null -ne $Offer -and (ConvertTo-NHReleaseVersion $Offer.version) -gt (Get-NHTrackBaseline $View)) {
        $View.State.Offer = $Offer
        if ($beta) {
            $View.Status.Text = (Get-NHBetaName $Offer.version) + ' is available.'
        } else {
            $View.Status.Text = 'Version ' + (Format-NHVersionLabel $Offer.version) + ' is available. Select Install update to accept.'
        }
    } elseif ($beta) {
        $View.Status.Text = 'No beta newer than your installed versions is available right now.'
    } elseif ($null -eq $View.State.Runtime) {
        $View.Status.Text = 'No stable release is available right now.'
    } else { $View.Status.Text = 'You have the latest stable release.' }
}

function Complete-NHPlayerInstall($View, [string]$Original) {
    # Re-read both selections; installing one track never changes the other.
    foreach ($track in @('stable','beta')) { $View.State.Runtimes[$track] = Get-NHTrackRuntime $Original $track }
    $View.State.Runtime = $View.State.Runtimes[$View.State.Track]
    Update-NHPlayerVersion $View
    $View.Status.Text = 'Update installed. Select Play to start.'
}

function Show-NHPlayerUpdateFailure($View, [string]$Operation, $Failure) {
    $exception = $Failure.Exception
    Write-NHLauncherLog ($Operation + ' failed: ' + (Format-NHLogError $Failure))
    # Play or closing the window stopped the check on purpose.
    if ($exception -is [OperationCanceledException]) { return }
    $hint = Get-NHLauncherLogHint
    if ($exception -is [TimeoutException]) { $View.Status.Text = $exception.Message + $hint; return }
    $message = ([string]$exception.Message).Trim()
    if (-not $message.EndsWith('.')) { $message += '.' }
    $View.Status.Text = 'Update unavailable: ' + $message + ' Your installed game is unchanged.' + $hint
}

function Invoke-NHPlayerUpdate($View, [string]$Original, [bool]$Install) {
    if ($View.State.Busy) { return }
    if ($Install -and $null -eq $View.State.Offer) { return }
    $directory = Join-Path ([IO.Path]::GetTempPath()) ('nh-launcher-' + [Guid]::NewGuid().ToString('N'))
    $process = $null
    $operation = if ($Install) { 'Install' } else { 'Check' }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Set-NHPlayerBusy $View $true (-not $Install)
    try {
        Assert-PlainPath $directory
        $null = [IO.Directory]::CreateDirectory($directory)
        $output = Join-Path $directory 'result.json'
        $status = Join-Path $directory 'status.txt'
        $request = Join-Path $directory 'accepted.json'
        $notes = Join-Path $directory 'notes.json'
        $View.Status.Text = 'Checking for updates...'
        if ($Install) {
            $View.Status.Text = 'Downloading and verifying accepted update...'
        } elseif ($View.State.Track -ceq 'beta') {
            $View.Status.Text = 'Checking for a beta release...'
        }
        if ($Install) { [IO.File]::WriteAllText($request, ($View.State.Offer | ConvertTo-Json -Compress)) }
        $arguments = @('-Operation',$operation,'-Original',$Original,'-Output',$output,'-StatusPath',$status,
            '-LogPath',$script:NHLogPath)
        if ($Install) { $arguments += @('-Request',$request) }
        else { $arguments += @('-Channel',$View.State.Track) + (Get-NHPlayerNotesArguments $View $notes) }
        # A handed-over launcher runs its own background operations.
        $script = Join-Path $View.State.Code 'Player-Launcher-Operation.ps1'
        Assert-PlainPath $script
        $process = New-NHPlayerProcess $Original $script $arguments
        $timeout = if ($Install) { 0 } else { $script:NHCheckDeadlineSeconds }
        Write-NHLauncherLog ($operation + ' child pid ' + $process.Id + ' started for the ' + $View.State.Track +
            ' track; deadline ' + $(if ($timeout) { [string]$timeout + ' s' } else { 'none' }))
        $code = Wait-NHPlayerProcess $process $View $status $timeout
        Write-NHLauncherLog ($operation + ' child exited with code ' + $code + ' after ' + $clock.ElapsedMilliseconds + ' ms')
        if (-not [IO.File]::Exists($output)) { throw ('The update ' + $operation.ToLowerInvariant() + ' stopped without a result (exit code ' + $code + ').') }
        $result = Read-NHSmallUpdateJson $output
        if ($code -ne 0 -or -not $result.ok) { throw $result.error }
        $View.State.Offer = $null
        if ($Install) { Complete-NHPlayerInstall $View $Original } else {
            Complete-NHPlayerCheck $View $result.value
            $View.State.Notes[$View.State.Track] = Read-NHReleaseNotesFile $notes
        }
    } catch {
        Show-NHPlayerUpdateFailure $View $operation $_
    } finally {
        if ($null -ne $process) { $process.Dispose() }
        if ([IO.Directory]::Exists($directory)) { [IO.Directory]::Delete($directory, $true) }
        Set-NHPlayerBusy $View $false
    }
    if ($View.State.CloseRequested) { $View.Form.Close(); return }
    if ($View.State.PlayRequested) {
        $View.State.PlayRequested = $false
        Invoke-NHPlayerGame $View $false
    }
}

function Request-NHPlayerGame($View) {
    # Play never waits for an update check: it stops the check, then starts the game.
    if (-not $View.State.Busy) { Invoke-NHPlayerGame $View $false; return }
    if (-not $View.State.Checking -or $null -eq $View.State.Runtime) { return }
    Write-NHLauncherLog 'Play selected during the update check; stopping the check'
    $View.State.PlayRequested = $true
    $View.Play.Enabled = $false
    $View.Status.Text = 'Stopping the update check and starting New Horizons...'
}

function Request-NHPlayerClose($View, $Closing) {
    # A busy launcher stays open; closing during an update check stops it and then closes.
    if (-not $View.State.Busy) { return }
    $Closing.Cancel = $true
    if ($View.State.Checking -and -not $View.State.CloseRequested) {
        Write-NHLauncherLog 'Window closed during the update check; stopping the check'
        $View.State.CloseRequested = $true
    }
}

function Invoke-NHPlayerGame($View, [bool]$Setup) {
    if ($View.State.Busy) { return }
    $process = $null
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $View.State.Launches++
    Set-NHPlayerBusy $View $true
    try {
        $View.Status.Text = if ($Setup) { 'Selecting original game files...' } else { 'Preparing game artwork and starting New Horizons...' }
        $arguments = @()
        if ($Setup) { $arguments = @('-SelectAssets','-SetupOnly') }
        $runtime = $View.State.Runtime
        # An empty track never plays the other track's release; it can still choose game files.
        if ($null -eq $runtime) {
            if (-not $Setup) { throw $(if ($View.State.Track -ceq 'beta') { 'No beta is installed.' } else { 'No stable version is installed.' }) }
            $runtime = $View.State.Runtimes[$View.State.Own]
        }
        $localData = Get-NHTrackLocalData $View.State.Track
        if ($localData -and -not $Setup) { $arguments = @(Get-NHBetaAssetArguments $localData) }
        $script = Join-Path $runtime 'Start-New-Horizons.ps1'
        Assert-PlainPath $script
        $process = New-NHPlayerProcess $runtime $script $arguments $localData
        $kind = if ($Setup) { 'Original game file selection' } else { 'Game' }
        Write-NHLauncherLog ($kind + ' pid ' + $process.Id + ' started: ' + $View.State.Track + ' track, ' + $runtime)
        $View.Form.Hide()
        $code = Wait-NHPlayerProcess $process $View $null
        Write-NHLauncherLog ($kind + ' exited with code ' + $code + ' after ' + [int]$clock.Elapsed.TotalSeconds + ' s')
        $View.State.ExitCode = $code
        $View.Status.Text = if ($code -eq 0) { 'Ready to play.' } else { 'New Horizons exited with status ' + $code + '.' }
    } catch {
        Write-NHLauncherLog ('Could not start: ' + (Format-NHLogError $_))
        $View.Status.Text = 'Could not start: ' + $_.Exception.Message + (Get-NHLauncherLogHint)
        $View.State.ExitCode = 1
    } finally {
        if ($null -ne $process) { $process.Dispose() }
        $View.Form.Show()
        Set-NHPlayerBusy $View $false
    }
}

function New-NHPlayerButton($Form, [string]$Text, [int]$X, [int]$Y, [int]$Width, [string]$Face = 'Button') {
    $button = New-Object Windows.Forms.Button
    $button.Text = $Text
    $button.Location = New-Object Drawing.Point($X,$Y)
    $button.Size = New-Object Drawing.Size($Width,36)
    Set-NHButtonLook $button $Face
    if ($Face -ceq 'Play') { $button.Font = New-Object Drawing.Font($Form.Font, [Drawing.FontStyle]::Bold) }
    $Form.Controls.Add($button)
    return $button
}

function New-NHPlayerLabel($Form, [string]$Text, [int]$X, [int]$Y, [int]$Width, [int]$Height) {
    $label = New-Object Windows.Forms.Label
    $label.Location = New-Object Drawing.Point($X,$Y)
    $label.Size = New-Object Drawing.Size($Width,$Height)
    $label.TextAlign = 'MiddleLeft'
    $label.Text = $Text
    $Form.Controls.Add($label)
    return $label
}

function New-NHPlayerTitle($Form) {
    # The Linux launcher's serif title; Georgia ships with Windows 10 and 11.
    $title = New-Object Windows.Forms.Label
    $title.AutoSize = $true
    $title.Location = New-Object Drawing.Point(18,16)
    $title.Font = New-NHLookFont 'Georgia' 20 'Bold'
    $title.ForeColor = Get-NHLookColor 'Title'
    $title.UseMnemonic = $false
    $title.Text = 'NEW HORIZONS'
    $Form.Controls.Add($title)
}

function New-NHPlayerTrack($Form, [string]$Track) {
    # PowerShell names are case-insensitive: $track would be the [string] parameter $Track.
    $null = New-NHPlayerLabel $Form 'Version track' 22 70 104 26
    $list = New-Object Windows.Forms.ComboBox
    Set-NHComboLook $list
    $list.Location = New-Object Drawing.Point(132,71)
    $list.Size = New-Object Drawing.Size(406,24)
    $null = $list.Items.Add('Stable')
    $null = $list.Items.Add('Beta (experimental, separate saves)')
    $list.SelectedIndex = [int]($Track -ceq 'beta')
    $Form.Controls.Add($list)
    return $list
}

function New-NHPlayerView($Runtimes, [string]$CodeRoot, [string]$Own = 'stable') {
    # Same rows as the Linux launcher: title, track, version and notes, a
    # wrapping status, progress, then Play and the other actions two by two.
    Initialize-NHWindowsLook
    $form = New-Object Windows.Forms.Form
    Start-NHFormLook $form 560 336
    $form.Text = 'New Horizons'
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    New-NHPlayerTitle $form
    $track = New-NHPlayerTrack $form $Own
    $version = New-NHPlayerLabel $form '' 22 108 396 32
    $notes = New-NHPlayerButton $form "What's new" 428 106 110
    $status = New-NHPlayerLabel $form 'Ready to play.' 22 150 516 54
    $status.TextAlign = 'TopLeft'
    $status.AutoEllipsis = $true
    $bar = New-Object Windows.Forms.ProgressBar
    $bar.Location = New-Object Drawing.Point(22,208)
    $bar.Size = New-Object Drawing.Size(516,12)
    $bar.Style = 'Marquee'
    $bar.MarqueeAnimationSpeed = 30
    $bar.Visible = $false
    $form.Controls.Add($bar)
    $view = [PSCustomObject]@{Form=$form;Version=$version;Status=$status;Bar=$bar;Track=$track;
        Play=(New-NHPlayerButton $form 'Play' 22 232 253 'Play');
        Check=(New-NHPlayerButton $form 'Check for updates' 285 232 253);
        Install=(New-NHPlayerButton $form 'Install update' 22 278 253);
        Assets=(New-NHPlayerButton $form 'Original game files' 285 278 253);
        Notes=$notes;
        State=[PSCustomObject]@{Runtime=$Runtimes[$Own];Offer=$null;Busy=$false;ExitCode=0;
            Track=$Own;Own=$Own;Runtimes=$Runtimes;Code=$CodeRoot;Notes=@{};Checking=$false;PlayRequested=$false;
            CloseRequested=$false;Launches=0}}
    $view.Install.Enabled = $false
    Update-NHPlayerVersion $view
    Complete-NHFormLook $form
    # Focus Play, as the Linux window shows no selected text field.
    if ($view.Play.Enabled) { $form.ActiveControl = $view.Play }
    return $view
}

function Test-NHLauncherAutoCheck([string]$Track = 'stable') {
    try {
        $local = Get-NHTrackLocalData $Track
        if (-not $local) { $local = $env:LOCALAPPDATA }
        $settings = Join-Path $local 'HeroesIII-NewHorizons\config\settings.json'
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
    Start-NHLauncherSession $CodeRoot
    # The launcher opens on the track the installation was published on.
    $own = Get-NHPackageChannel $Original
    $runtimes = @{stable=(Get-NHTrackRuntime $Original 'stable'); beta=(Get-NHTrackRuntime $Original 'beta')}
    $plays = { param($track) if ($runtimes[$track]) { 'plays ' + $runtimes[$track] } else { 'not installed' } }
    Write-NHLauncherLog ('Installation ' + $Original + ' (' + $own + ' package); stable ' + (& $plays 'stable') +
        '; beta ' + (& $plays 'beta'))
    $view = New-NHPlayerView $runtimes $CodeRoot $own
    try {
        $view.Form.add_FormClosing({ param($sender,$event) Request-NHPlayerClose $view $event })
        $view.Play.add_Click({ Request-NHPlayerGame $view })
        $view.Assets.add_Click({ Invoke-NHPlayerGame $view $true })
        $view.Check.add_Click({ Invoke-NHPlayerUpdate $view $Original $false })
        $view.Install.add_Click({ Invoke-NHPlayerUpdate $view $Original $true })
        $view.Notes.add_Click({ Show-NHPlayerNotes $view $Original })
        $view.Track.add_SelectedIndexChanged({ Set-NHPlayerTrack $view $Original $view.Track.SelectedIndex })
        $view.Form.add_Shown({ if (Test-NHLauncherAutoCheck $own) { Invoke-NHPlayerUpdate $view $Original $false } })
        $null = $view.Form.ShowDialog()
        Write-NHLauncherLog ('Launcher window closed; exit code ' + $view.State.ExitCode)
        return [int]$view.State.ExitCode
    } finally { $view.Form.Dispose() }
}
