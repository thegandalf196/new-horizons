# Synthetic Windows PowerShell 5.1 setup tests; never ship in the player ZIP.
# SPDX-License-Identifier: GPL-2.0-or-later
# ASCII source; non-ASCII fixture paths are constructed from Unicode code points.
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$fixture = $null
$checks = 0

function Assert-Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks++
    Write-Host "PASS: $Message"
}

function Get-ParsedScript([string]$Path) {
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors)
    if (@($errors).Count -gt 0) {
        throw ("PowerShell parse failed for ${Path}: " + (($errors | ForEach-Object { $_.Message }) -join '; '))
    }
    return $ast
}

function Write-FixtureFile([string]$Path, [string]$Text) {
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false))
}

function New-Assets([string]$Root, [string]$Label) {
    foreach ($archive in @('H3bitmap.lod', 'H3sprite.lod', 'H3ab_bmp.lod', 'H3ab_spr.lod')) {
        Write-FixtureFile (Join-Path $Root "Data\$archive") "SYNTHETIC NON-GAME ARCHIVE: $Label $archive"
    }
    Write-FixtureFile (Join-Path $Root 'Data\readiness.dat') "SYNTHETIC READINESS SENTINEL $Label"
    Write-FixtureFile (Join-Path $Root "Maps\nested [maps]\$script:oddName.h3m") "SYNTHETIC NON-GAME MAP $Label"
    Write-FixtureFile (Join-Path $Root "Mp3\$script:oddName.mp3") "SYNTHETIC NON-AUDIO FILE $Label"
    foreach ($file in @(Get-ChildItem -LiteralPath $Root -Recurse -File)) {
        [IO.File]::SetAttributes($file.FullName, [IO.FileAttributes]::ReadOnly)
    }
}

function Get-TreeFingerprint([string]$Root) {
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return (@(Get-ChildItem -LiteralPath $Root -Recurse -File -Force |
        ForEach-Object {
            $relative = $_.FullName.Substring($prefix.Length)
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            "$relative|$($_.Length)|$hash"
        } | Sort-Object) -join "`n")
}

function Quote-NativeArgument([string]$Value) {
    # All fixture arguments are files/non-root directories. Reject unsupported
    # quoting rather than joining an unquoted Start-Process -ArgumentList string.
    if ($Value.Contains('"') -or $Value.EndsWith('\')) {
        throw 'Invalid smoke-test argument for native command-line quoting.'
    }
    return '"' + $Value + '"'
}

function Invoke-Setup([string]$Source = '', [switch]$Reselect, [switch]$CancelFolder,
    [ValidateSet('None', 'Copy', 'Activation')][string]$Fault = 'None') {
    $arguments = @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $script:helper, '-SetupOnly')
    if ($Source) { $arguments += @('-AssetsPath', $Source) }
    if ($Reselect) { $arguments += '-SelectAssets' }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = Join-Path $PSHOME 'powershell.exe'
    $info.Arguments = ($arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    $info.WorkingDirectory = $script:package
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # Child-process only: do not change the invoking shell/user/machine profile.
    $info.EnvironmentVariables['LOCALAPPDATA'] = $script:privateData
    $info.EnvironmentVariables['NH_SETUP_SMOKE_FAIL_COPY'] = if ($Fault -eq 'Copy') { '1' } else { '0' }
    $info.EnvironmentVariables['NH_SETUP_SMOKE_FAIL_ACTIVATION'] = if ($Fault -eq 'Activation') { '1' } else { '0' }
    $info.EnvironmentVariables['NH_SETUP_SMOKE_CANCEL'] = if ($CancelFolder) { '1' } else { '0' }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'Could not start the setup smoke-test subprocess.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'Setup smoke subprocess timed out (60 seconds).'
        }
        return [PSCustomObject]@{ code = $process.ExitCode; output = $stdout.Result + $stderr.Result }
    } finally {
        $process.Dispose()
    }
}

try {
    Assert-Check ($PSVersionTable.PSVersion.Major -eq 5 -and $PSVersionTable.PSVersion.Minor -ge 1) 'running the Windows PowerShell 5.1 parser/runtime'
    $sourceDir = Split-Path -Parent $PSScriptRoot
    $sourceHelper = Join-Path $sourceDir 'Start-New-Horizons.ps1'
    $ast = Get-ParsedScript $sourceHelper
    $null = Get-ParsedScript $PSCommandPath
    Assert-Check ($null -ne $ast) 'original helper and smoke test parse without errors'

    # Quoting/path coverage includes spaces, Unicode, &, %, !, [], (), apostrophe.
    $script:oddName = "space & [x]!%(x)'" + [char]0x00E9 + [char]0x6F22
    $fixture = Join-Path ([IO.Path]::GetTempPath()) ('NH-setup-smoke-' + [Guid]::NewGuid().ToString('N'))
    $script:package = Join-Path $fixture ('package ' + $oddName)
    $script:privateData = Join-Path $fixture ('profile ' + $oddName)
    $sourceA = Join-Path $fixture ('Complete A ' + $oddName)
    $sourceB = Join-Path $fixture ('Complete B ' + $oddName)
    $invalid = Join-Path $fixture ('invalid ' + $oddName)
    $null = [IO.Directory]::CreateDirectory($privateData)
    $null = [IO.Directory]::CreateDirectory($invalid)
    Write-FixtureFile (Join-Path $package 'VCMI_client.exe') 'SYNTHETIC PLACEHOLDER, NOT AN EXECUTABLE'
    Write-FixtureFile (Join-Path $package 'VCMI_lib.dll') 'SYNTHETIC PLACEHOLDER, NOT A DLL'
    Write-FixtureFile (Join-Path $package 'config\filesystem.json') '{}'
    [IO.File]::Copy((Join-Path $sourceDir 'dirs.json'), (Join-Path $package 'config\dirs.json'))

    # TEMP COPY only: replace the picker, prohibit launch and insert two narrowly
    # located fault hooks. No production setup/copy/readiness statements are
    # removed. Even a regression cannot open a GUI or launch the placeholder EXE.
    $pickers = @($ast.FindAll({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Select-CompleteFolder'
    }, $true))
    Assert-Check ($pickers.Count -eq 1) 'exactly one folder-picker boundary can be guarded'
    $extent = $pickers[0].Extent
    $text = [IO.File]::ReadAllText($sourceHelper)
    $guard = @'
function Select-CompleteFolder {
    if ([Environment]::GetEnvironmentVariable('NH_SETUP_SMOKE_CANCEL') -eq '1') { return $null }
    throw 'SMOKE: folder picker is forbidden.'
}
function Start-Process { throw 'SMOKE: client launch is forbidden.' }
'@
    $guarded = $text.Substring(0, $extent.StartOffset) + $guard + $text.Substring($extent.EndOffset)
    $copyLine = '[IO.File]::Copy($from, $to, $false)'
    $activateLine = '[IO.Directory]::Move($stage, $content)'
    Assert-Check ([regex]::Matches($guarded, [regex]::Escape($copyLine)).Count -eq 1) 'copy fault hook has one exact insertion site'
    Assert-Check ([regex]::Matches($guarded, [regex]::Escape($activateLine)).Count -eq 1) 'activation fault hook has one exact insertion site'
    $copyFault = "if ([Environment]::GetEnvironmentVariable('NH_SETUP_SMOKE_FAIL_COPY') -eq '1') { throw 'SMOKE: injected partial-copy failure.' }"
    $activateFault = "if ([Environment]::GetEnvironmentVariable('NH_SETUP_SMOKE_FAIL_ACTIVATION') -eq '1') { throw 'SMOKE: injected activation failure.' }"
    $guarded = $guarded.Replace($copyLine, $copyLine + "`n" + $copyFault)
    $guarded = $guarded.Replace($activateLine, $activateFault + "`n" + $activateLine)
    $script:helper = Join-Path $package 'Start-New-Horizons.ps1'
    Write-FixtureFile $helper $guarded
    $null = Get-ParsedScript $helper
    Assert-Check ($guarded.Contains($guard)) 'temporary copy rejects both GUI and client launch'

    New-Assets $sourceA 'A'
    New-Assets $sourceB 'B'
    $sourceAHash = Get-TreeFingerprint $sourceA
    $sourceBHash = Get-TreeFingerprint $sourceB
    $outerLocalData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process')
    $profile = Join-Path $privateData 'HeroesIII-NewHorizons'
    $content = Join-Path $profile 'content'
    $marker = Join-Path $content '.nh-assets.json'

    $result = Invoke-Setup -Source $sourceA
    Assert-Check ($result.code -eq 0) "first synthetic import exits zero: $($result.output)"
    Assert-Check (Test-Path -LiteralPath $marker -PathType Leaf) 'completed import marker exists'
    foreach ($folder in @('config', 'cache', 'logs', 'Saves', 'content')) {
        Assert-Check (Test-Path -LiteralPath (Join-Path $profile $folder) -PathType Container) "private $folder exists"
    }
    $save = Join-Path $profile 'Saves\retain.vcgm1'
    $settings = Join-Path $profile 'config\settings.json'
    Write-FixtureFile $save 'SYNTHETIC SAVE SENTINEL'
    Write-FixtureFile $settings '{"syntheticSentinel":true}'
    $saveHash = (Get-FileHash -LiteralPath $save).Hash
    $settingsHash = (Get-FileHash -LiteralPath $settings).Hash
    $activeA = Get-TreeFingerprint $content
    $markerTime = (Get-Item -LiteralPath $marker).LastWriteTimeUtc.Ticks

    $result = Invoke-Setup
    Assert-Check ($result.code -eq 0) "repeat readiness succeeds without picker: $($result.output)"
    Assert-Check ((Get-TreeFingerprint $content) -ceq $activeA) 'repeat launch does not recopy or alter content'
    Assert-Check ((Get-Item -LiteralPath $marker).LastWriteTimeUtc.Ticks -eq $markerTime) 'repeat preserves original readiness record'
    Assert-Check (@(Get-ChildItem -LiteralPath $profile -Directory -Filter 'content.previous-*').Count -eq 0) 'repeat does not create a replacement backup'

    $result = Invoke-Setup -Source $invalid
    Assert-Check ($result.code -ne 0) 'invalid selection fails rather than starting game'
    Assert-Check ((Get-TreeFingerprint $content) -ceq $activeA) 'invalid selection retains last-good content'
    Assert-Check ((Get-FileHash -LiteralPath $save).Hash -ceq $saveHash) 'invalid selection preserves Saves'

    $result = Invoke-Setup -Source (Join-Path $fixture 'nonexistent installation')
    Assert-Check ($result.code -ne 0) 'missing source fails without picker or launch'
    Assert-Check ((Get-TreeFingerprint $content) -ceq $activeA) 'missing source retains last-good content'

    $result = Invoke-Setup -Reselect -CancelFolder
    Assert-Check ($result.code -eq 0 -and $result.output.Contains('Setup cancelled')) 'mocked folder cancellation exits cleanly without GUI'
    Assert-Check ((Get-TreeFingerprint $content) -ceq $activeA) 'cancellation retains last-good content'

    $busy = [IO.File]::Open((Join-Path $profile '.setup.lock'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try { $result = Invoke-Setup -Source $sourceB }
    finally { $busy.Dispose() }
    Assert-Check ($result.code -ne 0 -and $result.output.Contains('busy or not writable')) 'busy profile lock rejects another setup'
    Assert-Check ((Get-TreeFingerprint $content) -ceq $activeA) 'busy-lock failure preserves live content'

    foreach ($fault in @('Copy', 'Activation')) {
        $result = Invoke-Setup -Source $sourceB -Fault $fault
        Assert-Check ($result.code -ne 0 -and $result.output.Contains('SMOKE: injected')) "$fault failure is surfaced"
        Assert-Check ((Get-TreeFingerprint $content) -ceq $activeA) "$fault failure retains/restores last-good content"
        Assert-Check ((Get-FileHash -LiteralPath $save).Hash -ceq $saveHash) "$fault failure preserves Saves"
        Assert-Check (@(Get-ChildItem -LiteralPath $profile -Directory -Filter '.import-*').Count -eq 0) "$fault failure removes only its partial staging copy"
        Assert-Check (@(Get-ChildItem -LiteralPath $profile -Directory -Filter 'content.previous-*').Count -eq 0) "$fault failure leaves no displaced active-content backup"
    }

    $result = Invoke-Setup -Source $sourceB -Reselect
    Assert-Check ($result.code -eq 0) "reselection activates verified B copy: $($result.output)"
    $activeB = Get-TreeFingerprint $content
    Assert-Check ($activeB -cne $activeA) 'reselection changes active content'
    Assert-Check ((Get-FileHash -LiteralPath (Join-Path $content 'Data\readiness.dat')).Hash -ceq (Get-FileHash -LiteralPath (Join-Path $sourceB 'Data\readiness.dat')).Hash) 'active copy matches B source'
    $backups = @(Get-ChildItem -LiteralPath $profile -Directory -Filter 'content.previous-*')
    Assert-Check ($backups.Count -eq 1) 'reselection retains exactly one previous content copy'
    Assert-Check ((Get-TreeFingerprint $backups[0].FullName) -ceq $activeA) 'retained backup is the exact last-good A content'

    # A directory and four archives alone must NOT pass repeat readiness.
    $sentinel = Join-Path $content 'Data\readiness.dat'
    Remove-Item -LiteralPath $sentinel -Force
    $result = Invoke-Setup
    Assert-Check ($result.code -ne 0 -and $result.output.Contains('Imported file missing or changed in size') -and $result.output.Contains('SMOKE: folder picker is forbidden')) 'missing recorded file rejects readiness without opening GUI'
    [IO.File]::Copy((Join-Path $sourceB 'Data\readiness.dat'), $sentinel)
    $result = Invoke-Setup
    Assert-Check ($result.code -eq 0) 'restored content passes repeat readiness'
    Assert-Check ((Get-TreeFingerprint $content) -ceq $activeB) 'readiness rejection did not otherwise mutate active content'

    Assert-Check ((Get-FileHash -LiteralPath $save).Hash -ceq $saveHash) 'reselection and repeat preserve Saves'
    Assert-Check ((Get-FileHash -LiteralPath $settings).Hash -ceq $settingsHash) 'reselection and repeat preserve settings'
    Assert-Check ((Get-TreeFingerprint $sourceA) -ceq $sourceAHash) 'source A hashes unchanged'
    Assert-Check ((Get-TreeFingerprint $sourceB) -ceq $sourceBHash) 'source B hashes unchanged'
    Assert-Check ([Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process') -ceq $outerLocalData) 'parent LOCALAPPDATA unchanged'
    Assert-Check (@(Get-ChildItem -LiteralPath $profile -Directory -Filter '.import-*').Count -eq 0) 'no partial staging directory survives successful/error tests'
    Write-Host "SUCCESS: $checks synthetic setup checks. No original assets, GUI, CMD entrypoint or game execution tested."
} catch {
    [Console]::Error.WriteLine($_.ToString())
    exit 1
} finally {
    if ($fixture -and (Test-Path -LiteralPath $fixture)) {
        try { Remove-Item -LiteralPath $fixture -Recurse -Force }
        catch {
            [Console]::Error.WriteLine("Failed to clean this test's private fixture: $fixture")
            exit 1
        }
    }
}
