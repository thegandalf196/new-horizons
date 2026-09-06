# SPDX-License-Identifier: GPL-2.0-or-later
# Execute the real preset functions without importing assets or starting a client.
# Runs on PowerShell 5.1/7; a Linux run is NOT Windows setup/GUI acceptance.
[CmdletBinding()]
param([string]$SourceRoot = (Join-Path $PSScriptRoot '../../..'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $SourceRoot 'tools/windows/Start-New-Horizons.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Launcher syntax errors' }
foreach ($name in @('Assert-PlainPath', 'Set-ManagedPreset')) {
    $function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
    if (-not $function) { throw "Missing real launcher function: $name" }
    . ([scriptblock]::Create($function.Extent.Text))
}
function Assert-Value($Actual, $Expected, [string]$Message) {
    if ($Actual -cne $Expected) { throw "$Message (actual: $Actual)" }
}
function Assert-Failure([scriptblock]$Action) {
    $failed = $false
    try { & $Action } catch { $failed = $true }
    if (-not $failed) { throw 'Expected fail-closed behavior' }
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('nh-managed-preset-' + [Guid]::NewGuid().ToString('N'))
try {
    $package = Join-Path $root 'package'
    $profile = Join-Path $root 'profile'
    foreach ($folder in @('package', 'profile/config', 'profile/Saves')) {
        $null = [IO.Directory]::CreateDirectory((Join-Path $root $folder))
    }
    $settings = Join-Path $profile 'config/settings.json'
    $save = Join-Path $profile 'Saves/keep.vsgm1'
    [IO.File]::WriteAllText($settings, 'settings sentinel')
    [IO.File]::WriteAllText($save, 'save sentinel')
    $preset = Join-Path $profile 'config/modSettings.json'
    Set-ManagedPreset $package $profile
    $value = [IO.File]::ReadAllText($preset) | ConvertFrom-Json
    Assert-Value ($value.presets.default.mods -join ',') 'vcmi,core' 'Legacy package must not activate absent content'

    foreach ($relative in @('Mods/new-horizons/mod.json', 'config/newHorizonsCombat.json',
                           'config/schemas/newHorizonsCombat.json', 'config/newHorizonsMagic.json',
                           'config/schemas/newHorizonsMagic.json')) {
        $path = Join-Path $package $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, '{}')
    }
    [IO.File]::WriteAllText($preset, '{"activePreset":"unwanted","presets":{"unwanted":{"mods":["other-mod"]}}}')
    Set-ManagedPreset $package $profile
    $value = [IO.File]::ReadAllText($preset) | ConvertFrom-Json
    Assert-Value $value.activePreset 'default' 'Managed preset must be selected'
    Assert-Value ($value.presets.default.mods -join ',') 'vcmi,core,new-horizons' 'Mounted root module must be explicitly active'
    Assert-Value (@($value.presets.PSObject.Properties).Count) 1 'Unmanaged presets must not leak into curated launch'
    Assert-Value ([IO.File]::ReadAllText($settings)) 'settings sentinel' 'Settings changed'
    Assert-Value ([IO.File]::ReadAllText($save)) 'save sentinel' 'Save changed'
    $before = (Get-FileHash -LiteralPath $preset -Algorithm SHA256).Hash
    Remove-Item -LiteralPath (Join-Path $package 'config/newHorizonsMagic.json')
    Assert-Failure { Set-ManagedPreset $package $profile }
    Assert-Value (Get-FileHash -LiteralPath $preset -Algorithm SHA256).Hash $before 'Incomplete package must not replace last preset'
    Assert-Value (@(Get-ChildItem -LiteralPath (Join-Path $profile 'config') -Filter '.nh-mod-preset-*').Count) 0 'Temporary files leaked'
    Write-Host 'PASS: real managed preset create/replace, legacy fallback, fixed activation, fail-closed content, settings/save preservation'
} finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
