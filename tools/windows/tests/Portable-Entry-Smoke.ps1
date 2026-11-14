# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the built OS-only bootstrap against a synthetic PowerShell receiver.
# No game, original assets, folder dialog, profile or host input/audio is used.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$BootstrapPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Launch-Process.ps1')
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('NH-entry-' + [Guid]::NewGuid().ToString('N'))
$previousCapture = $env:NH_ENTRY_SMOKE_CAPTURE
$previousExit = $env:NH_ENTRY_SMOKE_EXIT
try {
    $odd = "space & [x]!%(x)'" + [char]0x00E9 + [char]0x6F22
    $root = Join-Path $temporary $odd
    $runtime = Join-Path $root 'runtime'
    $null = [IO.Directory]::CreateDirectory($runtime)
    $bootstrap = Join-Path $root 'new-horizons.exe'
    [IO.File]::Copy((Resolve-Path -LiteralPath $BootstrapPath).Path, $bootstrap)
    [IO.File]::WriteAllText((Join-Path $runtime 'new-horizons.exe'), 'NEVER EXECUTE: synthetic receiver only')
    [IO.File]::WriteAllText((Join-Path $runtime 'Launch-Process.ps1'), '# Synthetic receiver does not launch clients')
    [IO.File]::WriteAllText((Join-Path $runtime 'Setup-Progress.ps1'), '# Synthetic receiver does not open windows')
    $receiver = @'
$ErrorActionPreference = 'Stop'
$value = $env:NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1 | ConvertFrom-Json
if ($value.version -ne 1) { exit 99 }
[IO.File]::WriteAllText($env:NH_ENTRY_SMOKE_CAPTURE, ($value.arguments | ConvertTo-Json -Compress), [Text.Encoding]::Unicode)
exit ([int]$env:NH_ENTRY_SMOKE_EXIT)
'@
    [IO.File]::WriteAllText((Join-Path $runtime 'Start-New-Horizons.ps1'), $receiver)
    $env:NH_ENTRY_SMOKE_CAPTURE = Join-Path $temporary 'captured.json'
    $arguments = @('', 'normal', 'two words', '"quoted"', 'trailing\', 'two\\', 'a\"b',
        $odd, '--flag=hello world', '$(not evaluated); & exit 90', "line`nbreak")
    foreach ($expectedExit in @(0, 37, -1073741819)) {
        $env:NH_ENTRY_SMOKE_EXIT = [string]$expectedExit
        $actualExit = Invoke-NativeClient $bootstrap $temporary $arguments
        if ($actualExit -ne $expectedExit) { throw "Exit status changed: $actualExit vs $expectedExit" }
        $actual = @([IO.File]::ReadAllText($env:NH_ENTRY_SMOKE_CAPTURE, [Text.Encoding]::Unicode) | ConvertFrom-Json)
        if ($actual.Count -ne $arguments.Count) { throw 'Argument count changed.' }
        for ($index = 0; $index -lt $arguments.Count; $index++) {
            if ($actual[$index] -cne $arguments[$index]) { throw "Argument $index changed." }
        }
    }
    Write-Output 'Portable entry: actual native argv/Unicode/exit-status PASS; no game launched.'
} finally {
    $env:NH_ENTRY_SMOKE_CAPTURE = $previousCapture
    $env:NH_ENTRY_SMOKE_EXIT = $previousExit
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
