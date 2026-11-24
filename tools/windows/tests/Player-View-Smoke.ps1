# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1 smoke: build the real player launcher window (never shown)
# for both tracks and check its controls. Catches errors that only appear when the
# window is constructed, such as a control replaced by a string.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$sourceDir = Split-Path -Parent $PSScriptRoot
. (Join-Path $sourceDir 'Player-Launcher.ps1')

$failures = 0
function Assert-ViewCheck([bool]$Condition, [string]$Message) {
    if ($Condition) { Write-Output ('PASS: ' + $Message) }
    else { Write-Output ('FAIL: ' + $Message); $script:failures++ }
}

$codeRoot = Join-Path ([IO.Path]::GetTempPath()) ('NH-player-view-' + [guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($codeRoot)
try {
    foreach ($own in @('stable', 'beta')) {
        $view = New-NHPlayerView @{} $codeRoot $own
        try {
            Assert-ViewCheck ($view.Track -is [Windows.Forms.ComboBox]) ($own + ': the version track is a drop-down list')
            Assert-ViewCheck ($view.Track.Items.Count -eq 2) ($own + ': the track list offers Stable and Beta')
            Assert-ViewCheck ($view.Track.SelectedIndex -eq [int]($own -ceq 'beta')) ($own + ': the track list selects its own track')
            Assert-ViewCheck ($view.Track.DropDownStyle -eq 'DropDownList') ($own + ': the track list cannot be typed into')
            foreach ($name in @('Play', 'Check', 'Install', 'Assets', 'Notes')) {
                Assert-ViewCheck ($view.$name -is [Windows.Forms.Button]) ($own + ': ' + $name + ' is a button')
            }
            Assert-ViewCheck ($view.Version.Text.Length -gt 0) ($own + ': the version line has text')
        } finally {
            $view.Form.Dispose()
        }
    }
} catch {
    Write-Output ('FAIL: building the launcher window threw: ' + $_.Exception.Message)
    $failures++
} finally {
    Remove-Item -LiteralPath $codeRoot -Recurse -Force -ErrorAction SilentlyContinue
}
if ($failures -ne 0) { exit 1 }
exit 0
