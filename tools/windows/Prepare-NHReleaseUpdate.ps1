# Prepare a verified portable release without activating or executing it.
# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Keep this file ASCII.
[CmdletBinding()]
param([string]$Store, [string]$Request, [string]$StatusPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ReleaseAPI = 'https://api.github.com/repos/thegandalf196/new-horizons/releases/latest'
$script:ReleaseRoot = 'https://github.com/thegandalf196/new-horizons/releases/'
$script:MaximumArchiveBytes = 2147483648L
$script:MaximumExpandedBytes = 8589934592L
$script:MaximumMembers = 20000

function Assert-UpdatePlainPath([string]$Path) {
    $cursor = [IO.Path]::GetFullPath($Path)
    $leaf = $true
    while ($cursor) {
        $attributes = $null
        try { $attributes = [IO.File]::GetAttributes($cursor) }
        catch [IO.FileNotFoundException] { }
        catch [IO.DirectoryNotFoundException] { }
        if ($null -ne $attributes) {
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Update paths must not contain links or junctions.'
            }
            if (-not $leaf -and ($attributes -band [IO.FileAttributes]::Directory) -eq 0) {
                throw 'Update path ancestors must be directories.'
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
        $leaf = $false
    }
}

function Write-UpdateStatus([string]$Message) {
    if ($StatusPath) {
        Assert-UpdatePlainPath $StatusPath
        [IO.File]::WriteAllText($StatusPath, $Message, [Text.Encoding]::UTF8)
    }
}

function Get-UpdateRelativePath([string]$Name) {
    if (-not $Name -or $Name.Length -gt 240 -or $Name.Contains('\') -or
        $Name -match '[\x00-\x1f\x7f<>:"|?*]' -or $Name.StartsWith('/') -or $Name.EndsWith('/')) {
        throw 'Invalid archive path.'
    }
    foreach ($part in $Name.Split('/')) {
        if (-not $part -or $part -eq '.' -or $part -eq '..' -or
            $part.EndsWith('.') -or $part.EndsWith(' ') -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
            throw 'Invalid Windows archive path component.'
        }
    }
    return $Name
}

function Read-UpdateJSON([string]$Path, [long]$MaximumBytes) {
    Assert-UpdatePlainPath $Path
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.PSIsContainer -or $file.Length -le 0 -or $file.Length -gt $MaximumBytes) {
        throw 'Invalid update JSON file size.'
    }
    $text = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8)
    # PowerShell 7.5 otherwise changes ISO timestamp strings into DateTime.
    # Windows PowerShell 5.1 already preserves those strings.
    if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) {
        $value = ConvertFrom-Json -InputObject $text -DateKind String
    } else {
        $value = ConvertFrom-Json -InputObject $text
    }
    if ($null -eq $value -or $value -is [Array] -or $value -is [string]) {
        throw 'An update JSON object is required.'
    }
    return $value
}

function Read-UpdateRequest([string]$Path) {
    $value = Read-UpdateJSON $Path 16384
    $expected = @('format', 'version', 'name', 'url', 'bytes', 'sha256')
    $properties = @($value.PSObject.Properties.Name)
    if ($properties.Count -ne $expected.Count) { throw 'Invalid update request fields.' }
    $raw = [IO.File]::ReadAllText([IO.Path]::GetFullPath($Path))
    foreach ($key in $expected) {
        if (-not ($properties -ccontains $key) -or
            [regex]::Matches($raw, '"' + $key + '"\s*:').Count -ne 1) {
            throw 'Missing or duplicate update request field.'
        }
    }
    if (($value.format -isnot [int] -and $value.format -isnot [long]) -or $value.format -ne 1 -or
        $value.version -isnot [string] -or $value.version.Length -gt 32 -or $value.version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or
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

function Assert-UpdateTransportURI([Uri]$URI) {
    $hosts = @('api.github.com', 'github.com', 'release-assets.githubusercontent.com',
        'objects.githubusercontent.com', 'github-releases.githubusercontent.com')
    if (-not $URI.IsAbsoluteUri -or $URI.Scheme -cne 'https' -or
        $URI.Port -ne 443 -or $URI.UserInfo -or $URI.Fragment -or
        -not ($hosts -contains $URI.DnsSafeHost)) {
        throw 'Unsupported update transport destination.'
    }
}

function Invoke-UpdateDownload([string]$URL, [string]$Output, [long]$MaximumBytes, [long]$ExactBytes = 0) {
    $uri = [Uri]$URL
    $response = $null
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    try {
        for ($redirects = 0; $redirects -le 5; $redirects++) {
            Assert-UpdateTransportURI $uri
            $http = [Net.HttpWebRequest]::Create($uri)
            $http.AllowAutoRedirect = $false
            $http.Timeout = 30000
            $http.ReadWriteTimeout = 30000
            $http.UserAgent = 'New-Horizons-Portable-Updater'
            $http.Accept = 'application/vnd.github+json'
            $http.Credentials = $null
            $response = $http.GetResponse()
            $status = [int]$response.StatusCode
            if ($status -in @(301, 302, 303, 307, 308)) {
                $location = $response.Headers['Location']
                if (-not $location -or $redirects -eq 5) { throw 'Invalid update redirect.' }
                $uri = New-Object Uri($uri, $location)
                $response.Dispose()
                $response = $null
                continue
            }
            if ($status -ne 200) { throw 'Update download did not return HTTP 200.' }
            if ($response.ContentLength -gt $MaximumBytes -or
                ($ExactBytes -gt 0 -and $response.ContentLength -ge 0 -and $response.ContentLength -ne $ExactBytes)) {
                throw 'Update download length differs from its release metadata.'
            }
            $inputStream = $response.GetResponseStream()
            $outputStream = [IO.File]::Open($Output, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try {
                $buffer = New-Object byte[] 65536
                $count = 0L
                $clock = [Diagnostics.Stopwatch]::StartNew()
                while (($read = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $count += $read
                    if ($count -gt $MaximumBytes -or ($ExactBytes -gt 0 -and $count -gt $ExactBytes) -or
                        $clock.Elapsed.TotalMinutes -gt 15) { throw 'Update download exceeded its bound.' }
                    $outputStream.Write($buffer, 0, $read)
                }
                if ($ExactBytes -gt 0 -and $count -ne $ExactBytes) { throw 'Truncated update download.' }
            } finally {
                $outputStream.Dispose()
                $inputStream.Dispose()
            }
            return
        }
    } finally {
        if ($null -ne $response) { $response.Dispose() }
    }
}

function Assert-CurrentUpdateRelease($Offer, [string]$MetadataPath) {
    $release = Read-UpdateJSON $MetadataPath 1048576
    $published = [DateTimeOffset]::MinValue
    if ($release.draft -isnot [bool] -or $release.draft -or $release.prerelease -isnot [bool] -or
        $release.prerelease -or $release.tag_name -cne ('v' + $Offer.version) -or
        $release.html_url -cne ($script:ReleaseRoot + 'tag/v' + $Offer.version) -or
        $release.published_at -isnot [string] -or $release.published_at -cnotmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$' -or
        -not [DateTimeOffset]::TryParse($release.published_at, [ref]$published) -or
        $release.assets -isnot [Array] -or $release.assets.Count -gt 128) {
        throw 'The selected update is no longer the published stable release.'
    }
    $matches = @($release.assets | Where-Object { $_.name -cmatch '^New-Horizons-Windows-x64-[a-f0-9]{12}\.zip$' })
    if ($matches.Count -ne 1) { throw 'Ambiguous Windows release archive.' }
    $asset = $matches[0]
    if ($asset.name -cne $Offer.name -or $asset.browser_download_url -cne $Offer.url -or
        $asset.state -cne 'uploaded' -or ($asset.size -isnot [int] -and $asset.size -isnot [long]) -or
        $asset.size -ne $Offer.bytes -or
        $asset.digest -cne ('sha256:' + $Offer.sha256)) { throw 'Update asset metadata changed.' }
}

function Expand-UpdateArchive([string]$Archive, [string]$Destination, [string]$PackageRoot) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stream = $null
    $zip = $null
    try {
        $stream = [IO.File]::OpenRead($Archive)
        $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Read)
        if ($zip.Entries.Count -gt $script:MaximumMembers) { throw 'Too many update archive members.' }
        $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        $expanded = 0L
        $buffer = New-Object byte[] 65536
        foreach ($entry in $zip.Entries) {
            $directory = $entry.FullName.EndsWith('/')
            $name = $entry.FullName.TrimEnd('/')
            if ($directory -and $entry.FullName -cne ($name + '/')) { throw 'Noncanonical archive directory.' }
            $null = Get-UpdateRelativePath $name
            if (-not $seen.Add($name)) { throw 'Duplicate update archive member.' }
            $parts = $name.Split('/')
            if ($parts[0] -cne $PackageRoot -or ($parts.Count -eq 1 -and -not $directory)) {
                throw 'Update archive has an unexpected root.'
            }
            $mode = ([long]$entry.ExternalAttributes -shr 16) -band 61440
            if ($mode -notin @(0, 32768, 16384) -or ($entry.ExternalAttributes -band 1024) -ne 0 -or
                ($directory -and $mode -eq 32768) -or (-not $directory -and $mode -eq 16384)) {
                throw 'Linked or special update archive member.'
            }
            if ($entry.Length -lt 0 -or $entry.Length -gt $script:MaximumArchiveBytes) { throw 'Oversized archive member.' }
            $expanded += $entry.Length
            if ($expanded -gt $script:MaximumExpandedBytes) { throw 'Expanded update archive exceeds its limit.' }
            # Keep the authenticated archive root out of the installed path.
            $relative = if ($parts.Count -eq 1) { '' } else { $name.Substring($PackageRoot.Length + 1) }
            $path = if ($relative) { Join-Path $Destination $relative } else { $Destination }
            if ($env:OS -eq 'Windows_NT' -and $path.Length -ge 260) { throw 'Update path is too long.' }
            Assert-UpdatePlainPath $path
            if ($directory) {
                if ($entry.Length -ne 0) { throw 'Invalid directory archive member.' }
                $null = [IO.Directory]::CreateDirectory($path)
                continue
            }
            $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
            $source = $null
            $target = $null
            try {
                $source = $entry.Open()
                $target = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
                $copied = 0L
                while (($read = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $copied += $read
                    if ($copied -gt $entry.Length) { throw 'Archive member exceeds its declared size.' }
                    $target.Write($buffer, 0, $read)
                }
                if ($copied -ne $entry.Length) { throw 'Truncated archive member.' }
            } finally {
                try { if ($null -ne $target) { $target.Dispose() } }
                finally { if ($null -ne $source) { $source.Dispose() } }
            }
        }
    } finally {
        try { if ($null -ne $zip) { $zip.Dispose() } }
        finally { if ($null -ne $stream) { $stream.Dispose() } }
    }
}

function Assert-UpdatePayload([string]$Root, $Offer) {
    $manifestPath = Join-Path $Root 'runtime/SHA256SUMS.txt'
    Assert-UpdatePlainPath $manifestPath
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
        (Get-Item -LiteralPath $manifestPath).Length -gt 4194304) { throw 'Missing or oversized update manifest.' }
    $expected = New-Object 'System.Collections.Generic.Dictionary[string,string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in [IO.File]::ReadAllLines($manifestPath)) {
        if ($line -cnotmatch '^([a-f0-9]{64})  (.+)$') { throw 'Invalid update manifest record.' }
        $digest, $name = $Matches[1], $Matches[2]
        $null = Get-UpdateRelativePath $name
        if ($name -ceq 'runtime/SHA256SUMS.txt' -or $expected.ContainsKey($name)) { throw 'Duplicate or recursive manifest record.' }
        if ($name -cnotin @('new-horizons.exe', 'Setup.exe') -and -not $name.StartsWith('runtime/', [StringComparison]::Ordinal)) {
            throw 'Unexpected portable package root file.'
        }
        $expected.Add($name, $digest)
    }
    $entries = @(Get-ChildItem -LiteralPath $Root -Recurse -Force)
    foreach ($entry in $entries) { Assert-UpdatePlainPath $entry.FullName }
    $files = @($entries | Where-Object { -not $_.PSIsContainer })
    if ($files.Count -ne $expected.Count + 1) { throw 'Update payload differs from its manifest inventory.' }
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    foreach ($file in $files) {
        Assert-UpdatePlainPath $file.FullName
        $relative = $file.FullName.Substring($prefix.Length).Replace('\', '/')
        if ($relative -ceq 'runtime/SHA256SUMS.txt') { continue }
        if (-not $expected.ContainsKey($relative) -or
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expected[$relative]) {
            throw 'Update payload checksum mismatch.'
        }
    }
    foreach ($required in @('new-horizons.exe', 'Setup.exe', 'runtime/new-horizons.exe', 'runtime/VCMI_lib.dll',
        'runtime/Start-New-Horizons.ps1', 'runtime/Launch-Process.ps1', 'runtime/Setup-Progress.ps1',
        'runtime/Complete-Assets.ps1', 'runtime/Player-Launcher.ps1', 'runtime/Player-Launcher-Operation.ps1',
        'runtime/config/dirs.json', 'runtime/config/filesystem.json',
        'runtime/Mods/vcmi/mod.json', 'runtime/Mods/new-horizons/mod.json', 'runtime/Mods/new-horizons/NewHorizons.nhart',
        'runtime/config/newHorizonsCombat.json', 'runtime/config/schemas/newHorizonsCombat.json',
        'runtime/config/newHorizonsMagic.json', 'runtime/config/schemas/newHorizonsMagic.json',
        'runtime/BUILD-IDENTITY.json', 'runtime/config/newHorizonsVersion.json')) {
        if (-not $expected.ContainsKey($required)) { throw 'Incomplete portable update package.' }
    }
    Assert-UpdateArtHeader (Join-Path $Root 'runtime/Mods/new-horizons/NewHorizons.nhart')
    $version = Read-UpdateJSON (Join-Path $Root 'runtime/config/newHorizonsVersion.json') 65536
    $identity = Read-UpdateJSON (Join-Path $Root 'runtime/BUILD-IDENTITY.json') 2097152
    $revision = $Offer.name.Substring('New-Horizons-Windows-x64-'.Length, 12)
    if ($version.version -cne $Offer.version -or $identity.platform -cne 'Windows x64' -or
        $identity.project -cne 'Heroes III: New Horizons' -or $identity.source_commit -cnotmatch '^[a-f0-9]{40}$' -or
        -not $identity.source_commit.StartsWith($revision, [StringComparison]::Ordinal)) { throw 'Update package identity mismatch.' }
}

function Assert-UpdateArtHeader([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        $magic = $reader.ReadBytes(8)
        if ([BitConverter]::ToString($magic) -cne '4E-48-41-52-54-0D-0A-1A' -or
            $reader.ReadUInt32() -ne 1 -or $reader.ReadUInt32() -ne 0) { throw 'Invalid NHART header.' }
        $index = $reader.ReadUInt64()
        $count = $reader.ReadUInt64()
        if ($index -lt 32 -or $index -gt $stream.Length -or $count -lt 1 -or $count -gt 1000000) {
            throw 'Invalid NHART header bounds.'
        }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Prepare-NHReleaseUpdate([string]$InstallStore, [string]$RequestFile) {
    $offer = Read-UpdateRequest $RequestFile
    $root = [IO.Path]::GetFullPath($InstallStore)
    if ($root.StartsWith('\\') -or $root.StartsWith('//')) { throw 'Use a local update store.' }
    Assert-UpdatePlainPath $root
    $null = [IO.Directory]::CreateDirectory($root)
    $releases = Join-Path $root 'r'
    Assert-UpdatePlainPath $releases
    $null = [IO.Directory]::CreateDirectory($releases)
    $destination = Join-Path $releases $offer.sha256.Substring(0, 24)
    $stage = Join-Path $root ('.p-' + [Guid]::NewGuid().ToString('N').Substring(0, 16))
    $null = [IO.Directory]::CreateDirectory($stage)
    try {
        Write-UpdateStatus 'Checking published release...'
        $metadata = Join-Path $stage 'release.json'
        Invoke-UpdateDownload $script:ReleaseAPI $metadata 1048576
        Assert-CurrentUpdateRelease $offer $metadata
        $archive = Join-Path $stage 'release.zip'
        Write-UpdateStatus 'Downloading update...'
        Invoke-UpdateDownload $offer.url $archive $offer.bytes $offer.bytes
        Write-UpdateStatus 'Verifying archive checksum...'
        if ((Get-Item -LiteralPath $archive).Length -ne $offer.bytes -or
            (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -cne $offer.sha256) {
            throw 'Downloaded update checksum mismatch.'
        }
        $unpacked = Join-Path $stage 'p'
        $null = [IO.Directory]::CreateDirectory($unpacked)
        $stem = [IO.Path]::GetFileNameWithoutExtension($offer.name)
        Write-UpdateStatus 'Verifying and preparing game files...'
        Expand-UpdateArchive $archive $unpacked $stem
        $payload = $unpacked
        Assert-UpdatePayload $payload $offer
        Assert-UpdatePlainPath $destination
        if ($env:OS -eq 'Windows_NT') {
            $prefix = $payload.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
            foreach ($file in @(Get-ChildItem -LiteralPath $payload -Recurse -Force -File)) {
                if ((Join-Path $destination $file.FullName.Substring($prefix.Length)).Length -ge 260) {
                    throw 'The ready update path would exceed Windows path limits.'
                }
            }
        }
        if (Test-Path -LiteralPath $destination) {
            # A retry may reuse only the complete bytes verified again against
            # today's release and a newly downloaded archive. Never repair it.
            Assert-UpdatePayload $destination $offer
            $manifest = 'runtime/SHA256SUMS.txt'
            $freshHash = (Get-FileHash -LiteralPath (Join-Path $payload $manifest) -Algorithm SHA256).Hash
            $readyHash = (Get-FileHash -LiteralPath (Join-Path $destination $manifest) -Algorithm SHA256).Hash
            if ($freshHash -cne $readyHash) { throw 'Existing update differs from the verified release.' }
        } else {
            [IO.Directory]::Move($payload, $destination)
        }
        return [PSCustomObject]@{version = $offer.version; sha256 = $offer.sha256; ready = $destination}
    } finally {
        if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    if (-not $Store -or -not $Request) { throw 'Specify -Store and -Request.' }
    Prepare-NHReleaseUpdate $Store $Request | ConvertTo-Json -Compress
}
