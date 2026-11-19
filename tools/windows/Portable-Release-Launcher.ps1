# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Called by the portable player entry point.

function Read-NHSmallUpdateJson([string]$Path) {
    Assert-PlainPath $Path
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.PSIsContainer -or $file.Length -gt 8192) { throw 'Invalid update metadata file.' }
    return [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8) | ConvertFrom-Json
}

function Get-NHProductVersion([string]$Runtime) {
    $metadata = Read-NHSmallUpdateJson (Join-Path $Runtime 'config\newHorizonsVersion.json')
    if ($metadata.version -isnot [string] -or $metadata.version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Invalid New Horizons product version.'
    }
    return [Version]$metadata.version
}

function Get-NHSelectedUpdateRuntime([string]$Store, $Selection) {
    if ($Selection.format -ne 1 -or $Selection.sha256 -isnot [string] -or $Selection.sha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Invalid installed update selection.'
    }
    $runtime = Join-Path (Join-Path (Join-Path $Store 'r') ($Selection.sha256.Substring(0,24))) 'runtime'
    if ((Get-NHProductVersion $runtime).ToString() -cne $Selection.version) {
        throw 'Installed update version differs from its selection.'
    }
    foreach ($name in @('new-horizons.exe','VCMI_lib.dll','Start-New-Horizons.ps1','BUILD-IDENTITY.json',
                        'Launch-Process.ps1','Setup-Progress.ps1','Complete-Assets.ps1',
                        'Player-Launcher.ps1','Player-Launcher-Operation.ps1',
                        'config/dirs.json','config/filesystem.json', 'config/newHorizonsCombat.json',
                        'config/schemas/newHorizonsCombat.json', 'config/newHorizonsMagic.json',
                        'config/schemas/newHorizonsMagic.json', 'Mods/vcmi/mod.json',
                        'Mods/new-horizons/mod.json', 'Mods/new-horizons/NewHorizons.nhart')) {
        $file = Join-Path $runtime $name
        Assert-PlainPath $file
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'Updated runtime is incomplete.' }
    }
    return $runtime
}

function Test-NHUpdatedClient([string]$Runtime) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $Runtime 'new-horizons.exe'
    $start.WorkingDirectory = $Runtime
    $start.Arguments = '--version'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (-not $process.WaitForExit(20000)) {
            $process.Kill()
            $null = $process.WaitForExit(5000)
            throw 'The new client did not finish its startup check.'
        }
        if ($process.ExitCode -ne 0) { throw 'The new client failed its startup check.' }
    } finally {
        $process.Dispose()
    }
}

function Set-NHUpdateSelection([string]$Store, $Selection) {
    $current = Join-Path $Store 'current.json'
    $temporary = Join-Path $Store ('.activate-' + [Guid]::NewGuid().ToString('N'))
    Assert-PlainPath $current
    try {
        $stream = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew)
        try {
            $bytes = [Text.Encoding]::UTF8.GetBytes(($Selection | ConvertTo-Json -Compress))
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush($true)
        } finally {
            $stream.Dispose()
        }
        if ([IO.File]::Exists($current)) {
            [IO.File]::Replace($temporary, $current, [NullString]::Value)
        } else {
            [IO.File]::Move($temporary, $current)
        }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Install-NHRequestedRelease([string]$Original, [string]$Current, [string]$Store, [string]$Request, [string]$StatusPath) {
    $wanted = Read-NHSmallUpdateJson $Request
    if ($wanted.version -isnot [string] -or $wanted.version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Invalid requested update version.'
    }
    if ([Version]$wanted.version -le (Get-NHProductVersion $Current)) { return $Current }
    $worker = Join-Path $Original 'Prepare-NHReleaseUpdate.ps1'
    Assert-PlainPath $worker
    $prepared = (& $worker -Store $Store -Request $Request -StatusPath $StatusPath) | ConvertFrom-Json
    if ($prepared.version -cne $wanted.version -or $prepared.sha256 -cne $wanted.sha256) {
        throw 'Prepared update differs from the accepted release.'
    }
    $selection = [ordered]@{ format = 1; version = $prepared.version; sha256 = $prepared.sha256 }
    $updated = Get-NHSelectedUpdateRuntime $Store $selection
    Test-NHUpdatedClient $updated
    Set-NHUpdateSelection $Store $selection
    return $updated
}

function Select-NHPortableRelease([string]$Original, [bool]$ApplyRequest = $false) {
    # Read-only selection. Retired in-game request.json files are never consumed.
    $result = [PSCustomObject]@{ runtime = $Original; request = $null }
    try {
        Assert-PlainPath $Original
        if (-not (Test-Path -LiteralPath (Join-Path $Original 'BUILD-IDENTITY.json') -PathType Leaf)) { return $result }
        $store = Join-Path ([IO.Path]::GetDirectoryName($Original)) '.nhu'
        Assert-PlainPath $store
        $selection = Join-Path $store 'current.json'
        if (Test-Path -LiteralPath $selection -PathType Leaf) {
            $result.runtime = Get-NHSelectedUpdateRuntime $store (Read-NHSmallUpdateJson $selection)
        }
    } catch {
        [Console]::Error.WriteLine('New Horizons: Selected update is unavailable. Using the original installation.')
    }
    return $result
}
