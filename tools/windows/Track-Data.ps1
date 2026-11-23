# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Keep this file ASCII.
# Where each release track keeps its profile, saves, settings, cache and logs.
# The launcher and Start-New-Horizons.ps1 dot-source it; callers define Assert-PlainPath.
# The beta root is this folder below the player's LOCALAPPDATA.
$script:NHBetaDataName = 'NewHorizons-Beta'

function Get-NHTrackLocalData([string]$Track) {
    # Stable uses LOCALAPPDATA itself; beta keeps everything below its own root.
    if ($Track -ceq 'stable') { return '' }
    if ($Track -cne 'beta') { throw 'Unknown release track.' }
    $local = [Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process')
    if ([string]::IsNullOrWhiteSpace($local) -or -not [IO.Path]::IsPathRooted($local)) {
        throw 'LOCALAPPDATA is unavailable. Run as your normal Windows user.'
    }
    $root = Join-Path ([IO.Path]::GetFullPath($local)) $script:NHBetaDataName
    Assert-PlainPath $root
    return $root
}

function Get-NHBetaAssetArguments([string]$BetaLocalData) {
    # Beta reuses the stable verified copy of the original game files until the
    # player imports or selects its own. Setup verifies the copy as usual.
    $own = Join-Path $BetaLocalData 'HeroesIII-NewHorizons\content\.nh-assets.json'
    if ([IO.File]::Exists($own)) { return @() }
    $local = [IO.Path]::GetFullPath([Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process'))
    $stable = Join-Path $local 'HeroesIII-NewHorizons\content'
    if (-not [IO.File]::Exists((Join-Path $stable '.nh-assets.json'))) { return @() }
    return @('-AssetsPath', $stable)
}

function Copy-NHPlainSaveTree([string]$From, [string]$To) {
    # Ordinary folders and files only; linked entries are never followed or copied.
    $null = [IO.Directory]::CreateDirectory($To)
    foreach ($entry in (New-Object IO.DirectoryInfo($From)).GetFileSystemInfos()) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
        $target = Join-Path $To $entry.Name
        if ($entry -is [IO.DirectoryInfo]) { Copy-NHPlainSaveTree $entry.FullName $target }
        else { [IO.File]::Copy($entry.FullName, $target, $false) }
    }
}

function Copy-NHStableSavesToBeta([string]$LocalData) {
    # The first start that creates the beta root seeds it with a copy of the
    # stable saves: 0.93.0 and 0.93.1 beta packages kept theirs there, and a beta
    # reads older saves. The stable saves are never moved or changed.
    $root = [IO.Path]::GetFullPath($LocalData).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ([IO.Path]::GetFileName($root) -cne $script:NHBetaDataName -or
        [IO.Directory]::Exists($root) -or [IO.File]::Exists($root)) { return }
    $parent = [IO.Path]::GetDirectoryName($root)
    $source = Join-Path $parent 'new-horizons\Saves'
    try { Assert-PlainPath $source } catch { return } # A linked stable save folder is not copied.
    if (-not [IO.Directory]::Exists($source)) { return }
    $stage = Join-Path $parent ($script:NHBetaDataName + '.copying-' + [Guid]::NewGuid().ToString('N'))
    try {
        Copy-NHPlainSaveTree $source (Join-Path $stage 'new-horizons\Saves')
        # One rename on the same volume: the beta root appears complete or not at all.
        [IO.Directory]::Move($stage, $root)
        Write-Host ('Copied your saves to the beta: ' + (Join-Path $root 'new-horizons\Saves'))
    } catch {
        throw ('Could not copy your saves to the beta (' + $_.Exception.Message + '). Your saves are unchanged. Check free space and folder permissions, then try again.')
    } finally {
        if ([IO.Directory]::Exists($stage)) { [IO.Directory]::Delete($stage, $true) }
    }
}
