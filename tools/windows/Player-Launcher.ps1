# SPDX-License-Identifier: GPL-2.0-or-later
# Ordinary Windows EXE entry: presentation and exact owned child supervision.
# Setup, all CLI arguments and crash reporting retain Start-New-Horizons routes.
function New-NHPlayerProcess([string]$Runtime, [string]$Script, [string[]]$Arguments = @()) {
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
    foreach ($button in @($View.Play,$View.Check,$View.Assets)) { $button.Enabled = -not $Busy }
    $View.Install.Enabled = -not $Busy -and $null -ne $View.State.Offer
    $View.Bar.Visible = $Busy
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
        $operation = if ($Install) { 'Install' } else { 'Check' }
        $View.Status.Text = if ($Install) { 'Downloading and verifying accepted update...' } else { 'Checking for updates...' }
        if ($Install) { [IO.File]::WriteAllText($request, ($View.State.Offer | ConvertTo-Json -Compress)) }
        $arguments = @('-Operation',$operation,'-Original',$Original,'-Output',$output,'-StatusPath',$status)
        if ($Install) { $arguments += @('-Request',$request) }
        $script = Join-Path $Original 'Player-Launcher-Operation.ps1'
        Assert-PlainPath $script
        $process = New-NHPlayerProcess $Original $script $arguments
        $timeout = if ($Install) { 0 } else { 5 }
        $code = Wait-NHPlayerProcess $process $View $status $timeout
        $result = Read-NHSmallUpdateJson $output
        if ($code -ne 0 -or -not $result.ok) { throw $result.error }
        $View.State.Offer = $null
        if ($Install) {
            $View.State.Runtime = (Select-NHPortableRelease $Original $false).runtime
            $View.Version.Text = 'Installed version: ' + (Get-NHProductVersion $View.State.Runtime).ToString()
            $View.Status.Text = 'Update installed. Select Play to start.'
        } elseif ([Version]$result.value.version -gt (Get-NHProductVersion $View.State.Runtime)) {
            $View.State.Offer = $result.value
            $View.Status.Text = 'Version ' + $result.value.version + ' is available. Select Install update to accept.'
        } else { $View.Status.Text = 'You have the latest stable release.' }
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
        $script = Join-Path $runtime 'Start-New-Horizons.ps1'
        Assert-PlainPath $script
        $process = New-NHPlayerProcess $runtime $script $arguments
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

function New-NHPlayerView([string]$Runtime) {
    Add-Type -AssemblyName System.Windows.Forms
    $form = New-Object Windows.Forms.Form
    $form.Text = 'New Horizons'
    $form.ClientSize = New-Object Drawing.Size(500,240)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $version = New-Object Windows.Forms.Label
    $version.Location = New-Object Drawing.Point(18,18)
    $version.Size = New-Object Drawing.Size(460,24)
    $version.Text = 'Installed version: ' + (Get-NHProductVersion $Runtime).ToString()
    $form.Controls.Add($version)
    $status = New-Object Windows.Forms.Label
    $status.Location = New-Object Drawing.Point(18,138)
    $status.Size = New-Object Drawing.Size(460,52)
    $status.Text = 'Ready to play.'
    $form.Controls.Add($status)
    $bar = New-Object Windows.Forms.ProgressBar
    $bar.Location = New-Object Drawing.Point(18,203)
    $bar.Size = New-Object Drawing.Size(460,18)
    $bar.Style = 'Marquee'
    $bar.MarqueeAnimationSpeed = 30
    $bar.Visible = $false
    $form.Controls.Add($bar)
    $view = [PSCustomObject]@{Form=$form;Version=$version;Status=$status;Bar=$bar;
        Play=(New-NHPlayerButton $form 'Play' 18 54 460);
        Check=(New-NHPlayerButton $form 'Check for updates' 18 96 145);
        Install=(New-NHPlayerButton $form 'Install update' 173 96 135);
        Assets=(New-NHPlayerButton $form 'Original game files' 318 96 160);
        State=[PSCustomObject]@{Runtime=$Runtime;Offer=$null;Busy=$false;ExitCode=0}}
    $view.Install.Enabled = $false
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

function Show-NHPlayerLauncher([string]$Original) {
    $view = New-NHPlayerView (Select-NHPortableRelease $Original $false).runtime
    try {
        $view.Form.add_FormClosing({ param($sender,$event) if ($view.State.Busy) { $event.Cancel=$true } })
        $view.Play.add_Click({ Invoke-NHPlayerGame $view $false })
        $view.Assets.add_Click({ Invoke-NHPlayerGame $view $true })
        $view.Check.add_Click({ Invoke-NHPlayerUpdate $view $Original $false })
        $view.Install.add_Click({ Invoke-NHPlayerUpdate $view $Original $true })
        $view.Form.add_Shown({ if (Test-NHLauncherAutoCheck) { Invoke-NHPlayerUpdate $view $Original $false } })
        $null = $view.Form.ShowDialog()
        return [int]$view.State.ExitCode
    } finally { $view.Form.Dispose() }
}
