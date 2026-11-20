# Release tracks, update requests and published release metadata checks.
# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Keep this file ASCII. Dot-sourced by Prepare-NHReleaseUpdate.ps1.
$script:ReleaseAPIRoot = 'https://api.github.com/repos/thegandalf196/new-horizons/releases'
# Beta releases are ordinary GitHub prereleases; their track lists recent releases.
$script:ReleaseListAPI = $script:ReleaseAPIRoot + '?per_page=30'
$script:ReleaseVersionPattern = '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
$script:ReleaseAssetPattern = '^New-Horizons-Windows-x64-[a-f0-9]{12}\.zip$'

function Get-UpdateRequestChannel($Request) {
    # Format 1 requests predate tracks and always name the stable release.
    $property = $Request.PSObject.Properties['channel']
    if ($null -eq $property) { return 'stable' }
    if ($property.Value -isnot [string] -or ($property.Value -cne 'stable' -and $property.Value -cne 'beta')) {
        throw 'Unknown release track.'
    }
    return $property.Value
}

function Get-UpdateReleaseTagAPI([string]$Version) {
    if ($Version.Length -gt 32 -or $Version -cnotmatch $script:ReleaseVersionPattern) { throw 'Invalid release version.' }
    return $script:ReleaseAPIRoot + '/tags/v' + $Version
}

function Get-UpdateRevalidationAPI($Offer) {
    # A stable update must still be the official latest release; a beta must
    # still be published under its own tag.
    if ((Get-UpdateRequestChannel $Offer) -ceq 'beta') { return Get-UpdateReleaseTagAPI $Offer.version }
    return $script:ReleaseAPI
}

function Read-UpdateRequest([string]$Path) {
    $value = Read-UpdateJSON $Path 16384
    $expected = @('format', 'version', 'name', 'url', 'bytes', 'sha256')
    $properties = @($value.PSObject.Properties.Name)
    if ($properties -ccontains 'channel') { $expected += 'channel' }
    if ($properties.Count -ne $expected.Count) { throw 'Invalid update request fields.' }
    $raw = [IO.File]::ReadAllText([IO.Path]::GetFullPath($Path))
    foreach ($key in $expected) {
        if (-not ($properties -ccontains $key) -or
            [regex]::Matches($raw, '"' + $key + '"\s*:').Count -ne 1) {
            throw 'Missing or duplicate update request field.'
        }
    }
    $format = 1
    if ($expected.Count -eq 7) { $format = 2 }
    $null = Get-UpdateRequestChannel $value
    if (($value.format -isnot [int] -and $value.format -isnot [long]) -or $value.format -ne $format -or
        $value.version -isnot [string] -or $value.version.Length -gt 32 -or $value.version -cnotmatch $script:ReleaseVersionPattern -or
        $value.name -isnot [string] -or $value.name -cnotmatch '^New-Horizons-Windows-x64-([a-f0-9]{12})\.zip$' -or
        $value.url -isnot [string] -or
        $value.url -cne ($script:ReleaseRoot + 'download/v' + $value.version + '/' + $value.name) -or
        ($value.bytes -isnot [int] -and $value.bytes -isnot [long]) -or
        $value.bytes -le 0 -or $value.bytes -gt $script:MaximumArchiveBytes -or
        $value.sha256 -isnot [string] -or $value.sha256 -cnotmatch '^[a-f0-9]{64}$') {
        throw 'Invalid portable update request.'
    }
    return $value
}

function Assert-CurrentUpdateRelease($Offer, [string]$MetadataPath) {
    Assert-UpdateReleaseObject $Offer (Read-UpdateJSON $MetadataPath 1048576)
}

function Assert-UpdateReleaseObject($Offer, $Release) {
    $beta = (Get-UpdateRequestChannel $Offer) -ceq 'beta'
    $published = [DateTimeOffset]::MinValue
    if ($Release.draft -isnot [bool] -or $Release.draft -or $Release.prerelease -isnot [bool] -or
        ($Release.prerelease -and -not $beta) -or $Release.tag_name -cne ('v' + $Offer.version) -or
        $Release.html_url -cne ($script:ReleaseRoot + 'tag/v' + $Offer.version) -or
        $Release.published_at -isnot [string] -or $Release.published_at -cnotmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$' -or
        -not [DateTimeOffset]::TryParse($Release.published_at, [ref]$published) -or
        $Release.assets -isnot [Array] -or $Release.assets.Count -gt 128) {
        if ($beta) { throw 'The selected beta is no longer a published release.' }
        throw 'The selected update is no longer the published stable release.'
    }
    $archives = @($Release.assets | Where-Object { $_.name -cmatch $script:ReleaseAssetPattern })
    if ($archives.Count -ne 1) { throw 'Ambiguous Windows release archive.' }
    $asset = $archives[0]
    if ($asset.name -cne $Offer.name -or $asset.browser_download_url -cne $Offer.url -or
        $asset.state -cne 'uploaded' -or ($asset.size -isnot [int] -and $asset.size -isnot [long]) -or
        $asset.size -ne $Offer.bytes -or
        $asset.digest -cne ('sha256:' + $Offer.sha256)) { throw 'Update asset metadata changed.' }
}

function Read-UpdateReleaseList([string]$Path) {
    Assert-UpdatePlainPath $Path
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.PSIsContainer -or $file.Length -le 0 -or $file.Length -gt 4194304) { throw 'Invalid release list size.' }
    $text = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8)
    if (-not $text.TrimStart().StartsWith('[')) { throw 'Invalid release list.' }
    if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) {
        $value = ConvertFrom-Json -InputObject $text -DateKind String
    } else {
        $value = ConvertFrom-Json -InputObject $text
    }
    if ($null -eq $value) { return ,@() }
    $releases = @($value)
    if ($releases.Count -gt 100) { throw 'Invalid release list.' }
    return ,$releases
}

function Get-UpdateReleaseVersion($Release) {
    # The X.Y.Z version of a release-shaped object, or $null.
    if ($null -eq $Release -or $Release -is [string] -or $Release -is [ValueType]) { return $null }
    $tag = $Release.PSObject.Properties['tag_name']
    if ($null -eq $tag -or $tag.Value -isnot [string] -or $tag.Value.Length -gt 33 -or
        -not $tag.Value.StartsWith('v') -or $tag.Value.Substring(1) -cnotmatch $script:ReleaseVersionPattern) {
        return $null
    }
    return [Version]$tag.Value.Substring(1)
}

function Select-UpdateBetaRelease($Releases) {
    # The newest published release of either kind that carries one Windows package.
    $best = $null
    $bestVersion = $null
    foreach ($release in @($Releases)) {
        $version = Get-UpdateReleaseVersion $release
        if ($null -eq $version) { continue }
        $draft = $release.PSObject.Properties['draft']
        $assets = $release.PSObject.Properties['assets']
        if ($null -eq $draft -or $draft.Value -isnot [bool] -or $draft.Value -or
            $null -eq $assets -or $assets.Value -isnot [Array]) { continue }
        $packages = @($assets.Value | Where-Object {
            $null -ne $_ -and $_ -isnot [string] -and $null -ne $_.PSObject.Properties['name'] -and
            $_.name -is [string] -and $_.name -cmatch $script:ReleaseAssetPattern })
        if ($packages.Count -ne 1) { continue }
        if ($null -eq $best -or $version -gt $bestVersion) {
            $best = $release
            $bestVersion = $version
        }
    }
    return $best
}
