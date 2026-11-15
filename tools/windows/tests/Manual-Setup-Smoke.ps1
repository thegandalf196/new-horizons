# SPDX-License-Identifier: GPL-2.0-or-later
# Actual OS-only Setup.exe, synthetic PS5 receiver; no picker, assets or game.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$SetupPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Launch-Process.ps1')
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('NH-manual-' + [Guid]::NewGuid().ToString('N'))
$beforeArguments = $env:NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1
$beforeExit = $env:NH_MANUAL_SMOKE_EXIT
$beforeCapture = $env:NH_MANUAL_SMOKE_CAPTURE
try {
    $root = Join-Path $temporary ("space & [x] " + [char]0x00E9 + [char]0x6F22)
    $runtime = Join-Path $root 'runtime'
    $null = [IO.Directory]::CreateDirectory($runtime)
    $setup = Join-Path $root 'Setup.exe'
    [IO.File]::Copy((Resolve-Path -LiteralPath $SetupPath).Path, $setup)
    foreach ($name in @('new-horizons.exe', 'Launch-Process.ps1', 'Setup-Progress.ps1')) {
        [IO.File]::WriteAllText((Join-Path $runtime $name), 'SYNTHETIC: NEVER EXECUTE')
    }
    $receiver = @'
[CmdletBinding()]
param([switch]$SelectAssets, [switch]$SetupOnly)
$ErrorActionPreference = 'Stop'
if (-not $SelectAssets -or -not $SetupOnly) { exit 91 }
if ($env:NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1) { exit 92 }
[IO.File]::WriteAllText($env:NH_MANUAL_SMOKE_CAPTURE, 'setup-only: no game')
exit ([int]$env:NH_MANUAL_SMOKE_EXIT)
'@
    [IO.File]::WriteAllText((Join-Path $runtime 'Start-New-Horizons.ps1'), $receiver)
    $env:NH_MANUAL_SMOKE_CAPTURE = Join-Path $temporary 'capture.txt'
    # A stale caller's game envelope must never reach manual setup.
    $env:NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1 = '{"version":1,"arguments":["--version"]}'
    foreach ($expected in @(0, 1, 37)) {
        $env:NH_MANUAL_SMOKE_EXIT = [string]$expected
        if (Test-Path -LiteralPath $env:NH_MANUAL_SMOKE_CAPTURE) { Remove-Item -LiteralPath $env:NH_MANUAL_SMOKE_CAPTURE }
        $actual = Invoke-NativeClient $setup $temporary @()
        if ($actual -ne $expected) { throw "Setup exit changed: $actual vs $expected" }
        if ([IO.File]::ReadAllText($env:NH_MANUAL_SMOKE_CAPTURE) -cne 'setup-only: no game') { throw 'Setup receiver was not reached.' }
    }
    Write-Output 'Manual Setup: forced selection/setup-only, cleared game arguments and exact exits PASS.'
} finally {
    $env:NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1 = $beforeArguments
    $env:NH_MANUAL_SMOKE_EXIT = $beforeExit
    $env:NH_MANUAL_SMOKE_CAPTURE = $beforeCapture
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
