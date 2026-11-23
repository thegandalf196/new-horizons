# SPDX-License-Identifier: GPL-2.0-or-later
# System-only Windows PowerShell 5.1 background work for the player launcher.
[CmdletBinding()]
param([ValidateSet('Check','Install','SelfTest')][string]$Operation, [string]$Original,
    [string]$Output, [string]$Request, [string]$StatusPath,
    [ValidateSet('stable','beta')][string]$Channel = 'stable', [string]$NotesOutput = '', [string]$NotesFor = '',
    [string]$LogPath = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Prepare-NHReleaseUpdate.ps1') -Request $Request -StatusPath $StatusPath
. (Join-Path $PSScriptRoot 'Portable-Release-Launcher.ps1')
. (Join-Path $PSScriptRoot 'Player-Launcher-Notes.ps1')
. (Join-Path $PSScriptRoot 'Player-Launcher-Log.ps1')
function Assert-PlainPath([string]$Path) { Assert-UpdatePlainPath $Path }
# A stalled GitHub request fails with its own error before the window's check deadline.
$script:NHCheckRequestMilliseconds = 20000

function Get-NHPublishedRelease([string]$MetadataPath, [string]$Channel) {
    Set-NHCheckPhase $StatusPath 'request'
    if ($Channel -ceq 'beta') {
        Invoke-UpdateDownload $script:ReleaseListAPI $MetadataPath 4194304 0 $script:NHCheckRequestMilliseconds
        Set-NHCheckPhase $StatusPath 'release'
        return Select-UpdateBetaRelease (Read-UpdateReleaseList $MetadataPath)
    }
    Invoke-UpdateDownload $script:ReleaseAPI $MetadataPath 1048576 0 $script:NHCheckRequestMilliseconds
    Set-NHCheckPhase $StatusPath 'release'
    return Read-UpdateJSON $MetadataPath 1048576
}

function Get-NHLatestPlayerOffer([string]$MetadataPath, [string]$Channel = 'stable') {
    # Stable reads the official latest release; beta offers the newest published
    # release of either kind. Returns $null when the beta track has none.
    $release = Get-NHPublishedRelease $MetadataPath $Channel
    if ($null -eq $release) { return $null }
    # A stable release has no -beta.N suffix; a beta always has one.
    $version = Get-UpdateReleaseVersionText $release
    if ($null -eq $version -or (Test-NHBetaVersion $version) -ne ($Channel -ceq 'beta')) {
        throw 'The published release does not have a valid product version for its track.'
    }
    $assets = @($release.assets | Where-Object { $_.name -cmatch '^New-Horizons-Windows-x64-[a-f0-9]{12}\.zip$' })
    if ($assets.Count -ne 1) { throw 'The release does not have one Windows playable archive.' }
    $asset = $assets[0]
    if ($asset.digest -isnot [string] -or $asset.digest -cnotmatch '^sha256:[a-f0-9]{64}$') {
        throw 'The published archive has no authenticated SHA-256 digest.'
    }
    $offer = [PSCustomObject]@{format=1;version=$version;name=$asset.name;
        url=$asset.browser_download_url;bytes=$asset.size;sha256=$asset.digest.Substring(7)}
    if ($Channel -ceq 'beta') {
        $offer.format = 2
        $offer | Add-Member -NotePropertyName channel -NotePropertyValue 'beta'
    }
    # Apply the same field and release validators as installation, before showing an offer.
    $validation = $MetadataPath + '.offer'
    try {
        [IO.File]::WriteAllText($validation, ($offer | ConvertTo-Json -Compress))
        $offer = Read-UpdateRequest $validation
        Assert-UpdateReleaseObject $offer $release
        return $offer
    } finally {
        if ([IO.File]::Exists($validation)) { [IO.File]::Delete($validation) }
    }
}

function Install-NHPlayerOffer([string]$Original, [string]$Request, [string]$StatusPath, [string]$CodeRoot = '') {
    $store = Get-NHUpdateStore $Original $true
    Assert-PlainPath $store
    $null = [IO.Directory]::CreateDirectory($store)
    $lockPath = Join-Path $store 'lock'
    Assert-PlainPath $lockPath
    $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $current = (Select-NHPortableRelease $Original $false).runtime
        $updated = Install-NHRequestedRelease $Original $current $store $Request $StatusPath $CodeRoot
        return [PSCustomObject]@{runtime=$updated;version=(Get-NHProductVersionText $updated)}
    } finally { $lock.Dispose() }
}

function Write-NHOperationStart {
    Open-NHLauncherLog $LogPath $Operation.ToLowerInvariant()
    $started = 'unknown'
    try { $started = [string][int]([DateTime]::Now - [Diagnostics.Process]::GetCurrentProcess().StartTime).TotalMilliseconds } catch { }
    # Process creation to this line: interpreter start, antivirus scans and script loading.
    Write-NHLauncherLog ($Operation + ' child pid ' + $PID + ' running ' + $started + ' ms after its process started; ' +
        'PowerShell ' + $PSVersionTable.PSVersion + ', track ' + $Channel)
}

function Get-NHOperationError($Failure) {
    # Network failures name their cause for the player; others keep their own message.
    for ($inner = $Failure.Exception; $null -ne $inner; $inner = $inner.InnerException) {
        if ($inner -isnot [Net.WebException]) { continue }
        if ($inner.Response -is [Net.HttpWebResponse]) { return 'GitHub answered HTTP ' + [int]$inner.Response.StatusCode + '.' }
        return 'Could not reach GitHub (' + $inner.Status + '): ' + $inner.Message
    }
    return $Failure.Exception.Message
}

function Invoke-NHPlayerOperation {
    Write-NHOperationStart
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Assert-PlainPath $Output
    Assert-PlainPath $Original
    try {
        if ($Operation -eq 'Check') {
            Set-NHCheckPhase $StatusPath 'start'
            $metadata = $Output + '.release'
            try {
                $result = Get-NHLatestPlayerOffer $metadata $Channel
                # What's new notes come from this same response, never a second request.
                if ($NotesOutput) {
                    Set-NHCheckPhase $StatusPath 'notes'
                    $wanted = @($NotesFor)
                    if ($null -ne $result) { $wanted += $result.version }
                    Write-NHCheckNotes $metadata $Channel $wanted $NotesOutput
                }
            } finally { if ([IO.File]::Exists($metadata)) { [IO.File]::Delete($metadata) } }
        } else {
            $result = Install-NHPlayerOffer $Original $Request $StatusPath $PSScriptRoot
        }
        [IO.File]::WriteAllText($Output, (@{ok=$true;value=$result} | ConvertTo-Json -Depth 5 -Compress))
        $found = if ($null -eq $result) { 'nothing newer' } else { 'version ' + $result.version }
        Write-NHLauncherLog ($Operation + ' succeeded in ' + $clock.ElapsedMilliseconds + ' ms: ' + $found)
        return 0
    } catch {
        Write-NHLauncherLog ($Operation + ' failed after ' + $clock.ElapsedMilliseconds + ' ms: ' + (Format-NHLogError $_))
        [IO.File]::WriteAllText($Output, (@{ok=$false;error=(Get-NHOperationError $_)} | ConvertTo-Json -Compress))
        return 1
    }
}

function Test-NHPlayerLauncherCode {
    # An older original launcher hands over only after this package's launcher
    # code loads and parses under this Windows PowerShell.
    try {
        . (Join-Path $PSScriptRoot 'Launch-Process.ps1')
        . (Join-Path $PSScriptRoot 'Player-Launcher.ps1')
        foreach ($name in @('Show-NHPlayerLauncher','New-NHPlayerProcess','Select-NHPortableRelease',
                            'Resolve-NHLauncherOriginal','Prepare-NHReleaseUpdate','Read-UpdateRequest')) {
            if ($null -eq (Get-Command $name -CommandType Function -ErrorAction SilentlyContinue)) { return 1 }
        }
        foreach ($name in @('Start-New-Horizons.ps1','Setup-Progress.ps1','Complete-Assets.ps1')) {
            $tokens = $null
            $errors = $null
            $null = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name), [ref]$tokens, [ref]$errors)
            if (@($errors).Count -ne 0) { return 1 }
        }
        [Console]::Out.WriteLine($script:NHLauncherSelfTestToken)
        return 0
    } catch {
        return 1
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    if ($Operation -ceq 'SelfTest') { exit (Test-NHPlayerLauncherCode) }
    exit (Invoke-NHPlayerOperation)
}
