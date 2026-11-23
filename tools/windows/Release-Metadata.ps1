# Release tracks, update requests and published release metadata checks.
# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Keep this file ASCII. Dot-sourced by Prepare-NHReleaseUpdate.ps1.
$script:ReleaseAPIRoot = 'https://api.github.com/repos/thegandalf196/new-horizons/releases'
# Beta releases are ordinary GitHub prereleases; their track lists recent releases
# and only ever offers a prerelease, never a stable release.
$script:ReleaseListAPI = $script:ReleaseAPIRoot + '?per_page=30'
# Stable releases are MAJOR.MINOR.PATCH; Beta-track prereleases add -beta.N or -rc.N.
$script:ReleaseVersionPattern = '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-(beta|rc)\.([1-9][0-9]{0,8}))?$'
$script:ReleasePrereleaseNames = @{beta='Beta'; rc='Release Candidate'}
$script:ReleaseAssetPattern = '^New-Horizons-Windows-x64-[a-f0-9]{12}\.zip$'

function ConvertTo-NHReleaseVersion([string]$Text) {
    # Semantic-version precedence as a [Version] key, or $null for anything else. The
    # fourth part is beta N, rc 1000000000 + N (N has at most nine digits) or
    # Int32.MaxValue for a release, so 0.94.0-beta.9 < 0.94.0-beta.10 < 0.94.0-rc.1
    # < 0.94.0 < 0.94.1. Compare keys, never text.
    if ($Text.Length -gt 32 -or -not ($Text -cmatch $script:ReleaseVersionPattern)) { return $null }
    try {
        $rank = [int]::MaxValue
        if ($Matches[5] -ceq 'beta') { $rank = [int]$Matches[6] }
        if ($Matches[5] -ceq 'rc') { $rank = 1000000000 + [int]$Matches[6] }
        return New-Object Version([int]$Matches[1], [int]$Matches[2], [int]$Matches[3], $rank)
    } catch { return $null } # A part beyond Int32 is not a release version.
}

function Test-NHBetaVersion([string]$Text) {
    # Whether a version belongs on the Beta track: a -beta.N or -rc.N prerelease.
    $key = ConvertTo-NHReleaseVersion $Text
    return $null -ne $key -and $key.Revision -ne [int]::MaxValue
}

function Format-NHVersionLabel([string]$Text) {
    # The player-facing name: 0.94.0-beta.3 is "0.94.0 Beta 3", 0.94.0-rc.1 "0.94.0 Release Candidate 1".
    if ($null -eq (ConvertTo-NHReleaseVersion $Text) -or -not ($Text -cmatch $script:ReleaseVersionPattern) -or -not $Matches[5]) {
        return $Text
    }
    return $Matches[1] + '.' + $Matches[2] + '.' + $Matches[3] + ' ' + $script:ReleasePrereleaseNames[$Matches[5]] + ' ' + $Matches[6]
}

function Get-NHBetaName([string]$Version) {
    # A suffixed beta names itself (0.94.0 Beta 3); an older unsuffixed one is "Beta 0.93.1".
    $label = Format-NHVersionLabel $Version
    if ($label -cne $Version) { return $label }
    return 'Beta ' + $Version
}

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
    if ($null -eq (ConvertTo-NHReleaseVersion $Version)) { throw 'Invalid release version.' }
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
    $beta = (Get-UpdateRequestChannel $value) -ceq 'beta'
    # A beta has a -beta.N version and a stable release never has one.
    if (($value.format -isnot [int] -and $value.format -isnot [long]) -or $value.format -ne $format -or
        $value.version -isnot [string] -or $null -eq (ConvertTo-NHReleaseVersion $value.version) -or
        (Test-NHBetaVersion $value.version) -ne $beta -or
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
        $Release.prerelease -ne $beta -or (Test-NHBetaVersion $Offer.version) -ne $beta -or
        $Release.tag_name -cne ('v' + $Offer.version) -or
        $Release.html_url -cne ($script:ReleaseRoot + 'tag/v' + $Offer.version) -or
        $Release.published_at -isnot [string] -or $Release.published_at -cnotmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$' -or
        -not [DateTimeOffset]::TryParse($Release.published_at, [ref]$published) -or
        $Release.assets -isnot [Array] -or $Release.assets.Count -gt 128) {
        if ($beta) { throw 'The selected beta is no longer a published beta release.' }
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

function Get-UpdateReleaseVersionText($Release) {
    # The version of a release-shaped object tagged vVERSION, or $null.
    if ($null -eq $Release -or $Release -is [string] -or $Release -is [ValueType]) { return $null }
    $tag = $Release.PSObject.Properties['tag_name']
    if ($null -eq $tag -or $tag.Value -isnot [string] -or -not $tag.Value.StartsWith('v') -or
        $null -eq (ConvertTo-NHReleaseVersion $tag.Value.Substring(1))) {
        return $null
    }
    return $tag.Value.Substring(1)
}

function Get-UpdateReleaseVersion($Release) {
    # The precedence key of a release-shaped object, or $null.
    $text = Get-UpdateReleaseVersionText $Release
    if ($null -eq $text) { return $null }
    return ConvertTo-NHReleaseVersion $text
}

function Select-UpdateBetaRelease($Releases) {
    # The newest published prerelease with a -beta.N version and one Windows package.
    # A stable release, even one promoted from a beta, is never offered on the Beta track.
    $best = $null
    $bestVersion = $null
    foreach ($release in @($Releases)) {
        $version = Get-UpdateReleaseVersion $release
        if ($null -eq $version -or $version.Revision -eq [int]::MaxValue) { continue }
        $draft = $release.PSObject.Properties['draft']
        $prerelease = $release.PSObject.Properties['prerelease']
        $assets = $release.PSObject.Properties['assets']
        if ($null -eq $draft -or $draft.Value -isnot [bool] -or $draft.Value -or
            $null -eq $prerelease -or $prerelease.Value -isnot [bool] -or -not $prerelease.Value -or
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
