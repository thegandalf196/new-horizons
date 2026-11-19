# SPDX-License-Identifier: GPL-2.0-or-later
# System-only Windows PowerShell 5.1 background work for the player launcher.
[CmdletBinding()]
param([ValidateSet('Check','Install')][string]$Operation, [string]$Original,
    [string]$Output, [string]$Request, [string]$StatusPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Prepare-NHReleaseUpdate.ps1') -Request $Request -StatusPath $StatusPath
. (Join-Path $PSScriptRoot 'Portable-Release-Launcher.ps1')
function Assert-PlainPath([string]$Path) { Assert-UpdatePlainPath $Path }

function Get-NHLatestPlayerOffer([string]$MetadataPath) {
    Invoke-UpdateDownload $script:ReleaseAPI $MetadataPath 1048576
    $release = Read-UpdateJSON $MetadataPath 1048576
    if ($release.tag_name -isnot [string] -or
        $release.tag_name -cnotmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'The published release does not have a stable product version.'
    }
    $assets = @($release.assets | Where-Object { $_.name -cmatch '^New-Horizons-Windows-x64-[a-f0-9]{12}\.zip$' })
    if ($assets.Count -ne 1) { throw 'The release does not have one Windows playable archive.' }
    $asset = $assets[0]
    if ($asset.digest -isnot [string] -or $asset.digest -cnotmatch '^sha256:[a-f0-9]{64}$') {
        throw 'The published archive has no authenticated SHA-256 digest.'
    }
    $offer = [PSCustomObject]@{format=1;version=$release.tag_name.Substring(1);name=$asset.name;
        url=$asset.browser_download_url;bytes=$asset.size;sha256=$asset.digest.Substring(7)}
    # Apply the same field and release validators as installation, before showing an offer.
    $validation = $MetadataPath + '.offer'
    try {
        [IO.File]::WriteAllText($validation, ($offer | ConvertTo-Json -Compress))
        $offer = Read-UpdateRequest $validation
        Assert-CurrentUpdateRelease $offer $MetadataPath
        return $offer
    } finally {
        if ([IO.File]::Exists($validation)) { [IO.File]::Delete($validation) }
    }
}

function Install-NHPlayerOffer([string]$Original, [string]$Request, [string]$StatusPath) {
    $store = Join-Path ([IO.Path]::GetDirectoryName($Original)) '.nhu'
    Assert-PlainPath $store
    $null = [IO.Directory]::CreateDirectory($store)
    $lockPath = Join-Path $store 'lock'
    Assert-PlainPath $lockPath
    $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $current = (Select-NHPortableRelease $Original $false).runtime
        $updated = Install-NHRequestedRelease $Original $current $store $Request $StatusPath
        return [PSCustomObject]@{runtime=$updated;version=(Get-NHProductVersion $updated).ToString()}
    } finally { $lock.Dispose() }
}

function Invoke-NHPlayerOperation {
    Assert-PlainPath $Output
    Assert-PlainPath $Original
    try {
        if ($Operation -eq 'Check') {
            $metadata = $Output + '.release'
            try { $result = Get-NHLatestPlayerOffer $metadata }
            finally { if ([IO.File]::Exists($metadata)) { [IO.File]::Delete($metadata) } }
        } else {
            $result = Install-NHPlayerOffer $Original $Request $StatusPath
        }
        [IO.File]::WriteAllText($Output, (@{ok=$true;value=$result} | ConvertTo-Json -Depth 5 -Compress))
        return 0
    } catch {
        [IO.File]::WriteAllText($Output, (@{ok=$false;error=$_.Exception.Message} | ConvertTo-Json -Compress))
        return 1
    }
}

if ($MyInvocation.InvocationName -ne '.') { exit (Invoke-NHPlayerOperation) }
