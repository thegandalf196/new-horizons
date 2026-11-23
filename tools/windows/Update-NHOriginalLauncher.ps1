# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Keep this file ASCII.
# Launchers older than the hand-over (0.92.5 and earlier) run only the selected
# update's Start-New-Horizons.ps1. That update child upgrades the original
# installation's launcher scripts once, so the next start shows this launcher.
# Best effort: every failure leaves the original launcher working and the game starts.

# Launcher scripts copied from the update, in replacement order. Dependencies go
# first; Portable-Release-Launcher.ps1 carries the hand-over token and the
# Start-New-Horizons.ps1 entry point is replaced last. Identical files are skipped.
$script:NHOriginalLauncherFiles = @('Release-Metadata.ps1', 'Launch-Process.ps1', 'Setup-Progress.ps1',
    'Complete-Assets.ps1', 'Track-Data.ps1', 'Prepare-NHReleaseUpdate.ps1', 'Player-Launcher-Notes.ps1', 'Player-Launcher-Log.ps1',
    'Player-Launcher.ps1', 'Player-Launcher-Operation.ps1', 'Update-NHOriginalLauncher.ps1',
    'Portable-Release-Launcher.ps1', 'Start-New-Horizons.ps1')

function Resolve-NHExistingPathCase([string]$Path) {
    # The store records the installation upper-cased; Windows paths ignore case.
    if ([IO.Directory]::Exists($Path)) { return $Path }
    $parent = [IO.Path]::GetDirectoryName($Path)
    if (-not $parent) { return $null }
    $parent = Resolve-NHExistingPathCase $parent
    if (-not $parent) { return $null }
    $name = [IO.Path]::GetFileName($Path)
    $found = @([IO.Directory]::GetDirectories($parent) | Where-Object { [IO.Path]::GetFileName($_) -ieq $name })
    if ($found.Count -ne 1) { return $null }
    return $found[0]
}

function Get-NHUpdateChildOriginal([string]$Runtime) {
    # A selected update lives at <store>\r\<digest prefix>\runtime; the store's
    # installation.json names the folder holding the original runtime.
    $runtime = [IO.Path]::GetFullPath($Runtime).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ([IO.Path]::GetFileName($runtime) -ine 'runtime') { return $null }
    $digest = [IO.Path]::GetDirectoryName($runtime)
    if ([IO.Path]::GetFileName($digest) -cnotmatch '^[0-9a-f]{24}$') { return $null }
    $packages = [IO.Path]::GetDirectoryName($digest)
    if ([IO.Path]::GetFileName($packages) -cne 'r') { return $null }
    $store = [IO.Path]::GetDirectoryName($packages)
    $identity = Join-Path $store 'installation.json'
    Assert-PlainPath $identity
    if (-not [IO.File]::Exists($identity) -or (New-Object IO.FileInfo($identity)).Length -gt 32768) { return $null }
    $record = [IO.File]::ReadAllText($identity, [Text.Encoding]::UTF8) | ConvertFrom-Json
    if ($record.format -ne 1 -or $record.installation -isnot [string] -or
        -not [IO.Path]::IsPathRooted($record.installation)) { return $null }
    $installation = Resolve-NHExistingPathCase ([IO.Path]::GetFullPath($record.installation))
    if (-not $installation) { return $null }
    $original = Resolve-NHExistingPathCase (Join-Path $installation 'runtime')
    if (-not $original -or $original -ieq $runtime) { return $null }
    return [PSCustomObject]@{ original = $original; store = $store; runtime = $runtime }
}

function Test-NHOriginalLauncherCurrent([string]$Original) {
    # Cheap check, no child process: a launcher with the hand-over has the token.
    $selector = Join-Path $Original 'Portable-Release-Launcher.ps1'
    Assert-PlainPath $selector
    if (-not [IO.File]::Exists($selector)) { return $true }
    return ([IO.File]::ReadAllText($selector)).Contains('NH_WINDOWS_PORTABLE_LAUNCHER_V2')
}

function Assert-NHUpdateChildSelected($Child) {
    # Only the stable selection of this installation's own store may upgrade it.
    if (-not [IO.File]::Exists((Join-Path $Child.original 'BUILD-IDENTITY.json'))) { throw 'The original installation has no build identity.' }
    if ((Get-NHUpdateStore $Child.original) -ine $Child.store) { throw 'The update store belongs to another installation.' }
    $selection = Join-Path $Child.store 'current.json'
    Assert-PlainPath $selection
    if (-not [IO.File]::Exists($selection)) { throw 'The installation has no selected update.' }
    $selected = Get-NHSelectedUpdateRuntime $Child.store (Read-NHSmallUpdateJson $selection)
    if ([IO.Path]::GetFullPath($selected).TrimEnd([IO.Path]::DirectorySeparatorChar) -ine $Child.runtime) {
        throw 'This update is not the selected update of its installation.'
    }
    if ((Get-NHProductVersion $Child.runtime) -le (Get-NHProductVersion $Child.original)) {
        throw 'The selected update is not newer than the original installation.'
    }
}

function Get-NHLauncherUpgradeFiles([string]$Original, [string]$Runtime) {
    foreach ($name in $script:NHOriginalLauncherFiles) {
        $source = Join-Path $Runtime $name
        $target = Join-Path $Original $name
        Assert-PlainPath $source
        Assert-PlainPath $target
        if (-not [IO.File]::Exists($source)) {
            if ($name -ceq 'Update-NHOriginalLauncher.ps1') { continue }
            throw "The update has no $name."
        }
        $existed = [IO.File]::Exists($target)
        if ($existed -and (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ceq
            (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) { continue }
        [PSCustomObject]@{ name = $name; source = $source; target = $target; existed = $existed; stage = $null }
    }
}

function Backup-NHOriginalLauncher([string]$Backup, $Files) {
    # The backup also marks this upgrade as attempted, so it never repeats.
    $null = [IO.Directory]::CreateDirectory($Backup)
    foreach ($file in $Files) {
        if ($file.existed) { [IO.File]::Copy($file.target, (Join-Path $Backup $file.name), $false) }
    }
    $record = [ordered]@{ format = 1; files = @($Files | ForEach-Object { [ordered]@{ name = $_.name; existed = $_.existed } }) }
    [IO.File]::WriteAllText((Join-Path $Backup 'backup.json'), ($record | ConvertTo-Json -Depth 4 -Compress))
}

function Restore-NHOriginalLauncher([string]$Backup, $Files) {
    foreach ($file in $Files) {
        if ($file.existed) {
            [IO.File]::Copy((Join-Path $Backup $file.name), $file.target, $true)
        } elseif ([IO.File]::Exists($file.target)) {
            [IO.File]::Delete($file.target)
        }
    }
}

function Install-NHOriginalLauncherFiles($Files) {
    # Stage every copy next to its target and verify it before any replacement.
    foreach ($file in $Files) {
        $file.stage = $file.target + '.nh-new-' + [Guid]::NewGuid().ToString('N')
        [IO.File]::Copy($file.source, $file.stage, $false)
        if ((Get-FileHash -LiteralPath $file.stage -Algorithm SHA256).Hash -cne
            (Get-FileHash -LiteralPath $file.source -Algorithm SHA256).Hash) { throw 'A staged launcher file differs from the update.' }
    }
    foreach ($file in $Files) {
        if ([IO.File]::Exists($file.target)) {
            [IO.File]::Replace($file.stage, $file.target, [NullString]::Value)
        } else {
            [IO.File]::Move($file.stage, $file.target)
        }
        $file.stage = $null
    }
}

function Update-NHOriginalLauncherFiles($Child, [string]$Backup) {
    $files = @(Get-NHLauncherUpgradeFiles $Child.original $Child.runtime)
    if ($files.Count -eq 0) { return }
    Backup-NHOriginalLauncher $Backup $files
    try {
        Install-NHOriginalLauncherFiles $files
        if (-not (Test-NHLauncherSelfTest $Child.original)) { throw 'The upgraded launcher failed its self-test.' }
    } catch {
        $failure = $_.Exception.Message
        Restore-NHOriginalLauncher $Backup $files
        throw "$failure The previous launcher was restored."
    } finally {
        foreach ($file in $files) {
            if ($file.stage -and [IO.File]::Exists($file.stage)) { [IO.File]::Delete($file.stage) }
        }
    }
    Write-Host 'New Horizons: The launcher was upgraded. It opens the next time you start New Horizons.'
}

function Update-NHOriginalLauncher([string]$Runtime) {
    try {
        $child = Get-NHUpdateChildOriginal $Runtime
        if ($null -eq $child -or (Test-NHOriginalLauncherCurrent $child.original)) { return }
        # Load the update helpers only for an older original, in this function's scope.
        if ($null -eq (Get-Command Get-NHSelectedUpdateRuntime -CommandType Function -ErrorAction SilentlyContinue)) {
            . (Join-Path $child.runtime 'Portable-Release-Launcher.ps1')
        }
        Assert-NHUpdateChildSelected $child
        $backup = Join-Path $child.store ('launcher-backup-' + (Get-NHProductVersionText $child.original))
        Assert-PlainPath $backup
        if ([IO.Directory]::Exists($backup)) { return }
        $lockPath = Join-Path $child.store 'lock'
        Assert-PlainPath $lockPath
        # An update installation in progress holds this lock; then try another time.
        $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        try { Update-NHOriginalLauncherFiles $child $backup }
        finally { $lock.Dispose() }
    } catch {
        [Console]::Error.WriteLine('New Horizons: The original launcher was not upgraded: ' + $_.Exception.Message)
    }
}
