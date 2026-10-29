# SPDX-License-Identifier: GPL-2.0-or-later
# Runs only the production predicate, never the launcher/setup body.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot '..\Start-New-Horizons.ps1'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) { throw 'Launcher parse failed.' }
$function = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Test-ReportableGameExit'
}, $false)
if (-not $function) { throw 'Production exit classifier is missing.' }
$classify = $function.Body.GetScriptBlock()
$accessViolation = [BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]3221225477), 0)
$cases = @(
    @{ Code = 0; Stop = $false; Expected = $false },
    @{ Code = 1; Stop = $false; Expected = $false },
    @{ Code = 3; Stop = $false; Expected = $true },
    @{ Code = 3; Stop = $true; Expected = $false },
    @{ Code = $accessViolation; Stop = $false; Expected = $true },
    @{ Code = $accessViolation; Stop = $true; Expected = $false },
    @{ Code = -1073741510; Stop = $false; Expected = $false }
)
foreach ($case in $cases) {
    $actual = & $classify -ExitCode $case.Code -ForwardedStop $case.Stop
    if ($actual -ne $case.Expected) { throw "Wrong classification for $($case.Code), forwarded=$($case.Stop)." }
}
Write-Output 'Crash report exit classification: PASS'
