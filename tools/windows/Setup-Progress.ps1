# SPDX-License-Identifier: GPL-2.0-or-later
# Optional presentation only; asset validation and activation stay in setup.
function Show-AssetImportProgress([int]$Files, [long]$Bytes, [switch]$StartingGame) {
    if ($env:NH_WINDOWS_BOOTSTRAP_UI -ne '1') { return $null }
    Add-Type -AssemblyName System.Windows.Forms
    $form = New-Object Windows.Forms.Form
    $form.Text = if ($StartingGame) { 'New Horizons - starting game' } else { 'New Horizons - preparing original game assets' }
    $form.ClientSize = New-Object Drawing.Size(500, 125)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.ControlBox = $false
    $form.add_FormClosing({ param($sender, $event) if (-not $sender.Tag) { $event.Cancel = $true } })
    $label = New-Object Windows.Forms.Label
    $label.Location = New-Object Drawing.Point(15, 15)
    $label.Size = New-Object Drawing.Size(470, 55)
    $label.Text = if ($StartingGame) { 'Preparing game artwork and starting New Horizons...' } else { 'Copying and verifying assets. Please keep this window open.' }
    $bar = New-Object Windows.Forms.ProgressBar
    $bar.Location = New-Object Drawing.Point(15, 80)
    $bar.Size = New-Object Drawing.Size(470, 22)
    $bar.Maximum = 1000
    if ($StartingGame) {
        $bar.Style = 'Marquee'
        $bar.MarqueeAnimationSpeed = 30
    }
    $form.Controls.Add($label)
    $form.Controls.Add($bar)
    $form.Show()
    $form.Refresh()
    [Windows.Forms.Application]::DoEvents()
    return [PSCustomObject]@{ Form = $form; Label = $label; Bar = $bar; Files = $Files; Bytes = $Bytes }
}

function Update-AssetImportProgress($Progress, [int]$Completed, [long]$Bytes, [string]$Phase) {
    if ($null -eq $Progress) { return }
    $Progress.Label.Text = ('{0}' + "`r`n" + '{1}/{2} files verified; {3:N1}/{4:N1} MiB') -f
        $Phase, $Completed, $Progress.Files, ($Bytes / 1MB), ($Progress.Bytes / 1MB)
    $fraction = if ($Progress.Bytes -gt 0) { $Bytes / [double]$Progress.Bytes } else { 0 }
    $Progress.Bar.Value = [Math]::Min(1000, [Math]::Max(0, [int](1000 * $fraction)))
    $Progress.Form.Refresh()
    [Windows.Forms.Application]::DoEvents()
}

function Close-AssetImportProgress($Progress) {
    if ($null -ne $Progress) {
        try {
            if (-not $Progress.Form.IsDisposed) {
                $Progress.Form.Tag = $true
                $Progress.Form.Close()
                $Progress.Form.Dispose()
            }
        } catch {
            # Optional presentation must not interrupt the owned game process.
        }
    }
}

function Show-GameStartupProgress($Progress, [AllowEmptyCollection()][string[]]$Arguments = @()) {
    # The ordinary double-click entry sets this flag. Command-line and report
    # invocations keep their terminal/automation contract, including headless.
    if ($env:NH_WINDOWS_BOOTSTRAP_UI -cne '1' -or $Arguments.Count -ne 0) {
        Close-AssetImportProgress $Progress
        return $null
    }
    try {
        if ($null -eq $Progress) { $Progress = Show-AssetImportProgress 0 0 -StartingGame }
        if ($null -eq $Progress) { return $null }
        $Progress.Form.Text = 'New Horizons - starting game'
        $Progress.Label.Text = 'Preparing game artwork and starting New Horizons...'
        $Progress.Bar.Style = 'Marquee'
        $Progress.Bar.MarqueeAnimationSpeed = 30
        Update-GameStartupProgress $Progress
        return $Progress
    } catch {
        Close-AssetImportProgress $Progress
        return $null
    }
}

function Update-GameStartupProgress($Progress) {
    if ($null -eq $Progress) { return }
    $Progress.Form.Refresh()
    [Windows.Forms.Application]::DoEvents()
}
