# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Called by the portable player entry point.
# Load this package's own worker unless this script scope already has it. A
# launcher handed over in-process must not reuse the original's older copy.
if ($null -eq (Get-Variable -Name ReleaseRoot -Scope Script -ErrorAction SilentlyContinue)) {
    . (Join-Path $PSScriptRoot 'Prepare-NHReleaseUpdate.ps1')
}

function Read-NHSmallUpdateJson([string]$Path) {
    Assert-PlainPath $Path
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.PSIsContainer -or $file.Length -gt 8192) { throw 'Invalid update metadata file.' }
    return [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8) | ConvertFrom-Json
}

function Get-NHProductVersionText([string]$Runtime) {
    $metadata = Read-NHSmallUpdateJson (Join-Path $Runtime 'config\newHorizonsVersion.json')
    if ($metadata.version -isnot [string] -or $null -eq (ConvertTo-NHReleaseVersion $metadata.version)) {
        throw 'Invalid New Horizons product version.'
    }
    return $metadata.version
}

function Get-NHProductVersion([string]$Runtime) {
    # The installed version's precedence key (ConvertTo-NHReleaseVersion); display Get-NHProductVersionText.
    return ConvertTo-NHReleaseVersion (Get-NHProductVersionText $Runtime)
}

function Get-NHSelectedUpdateRuntime([string]$Store, $Selection) {
    if ($Selection.format -ne 1 -or $Selection.sha256 -isnot [string] -or $Selection.sha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Invalid installed update selection.'
    }
    $runtime = Join-Path (Join-Path (Join-Path $Store 'r') ($Selection.sha256.Substring(0,24))) 'runtime'
    if ((Get-NHProductVersionText $runtime) -cne $Selection.version) {
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
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        # The first start of a new executable can wait for an antivirus scan.
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            $null = $process.WaitForExit(5000)
            Write-UpdateLog 'The new client --version check timed out after 60 s'
            throw 'The new client did not finish its startup check.'
        }
        Write-UpdateLog ('The new client --version check exited with code ' + $process.ExitCode + ' in ' + $clock.ElapsedMilliseconds + ' ms')
        if ($process.ExitCode -ne 0) { throw 'The new client failed its startup check.' }
    } finally {
        $process.Dispose()
    }
}

function Get-NHTrackSelectionName([string]$Channel) {
    # Each track selects its own verified package; packages are shared by digest.
    if ($Channel -ceq 'stable') { return 'current.json' }
    if ($Channel -ceq 'beta') { return 'beta.json' }
    throw 'Unknown release track.'
}

function Set-NHUpdateSelection([string]$Store, $Selection, [string]$Name = 'current.json') {
    $current = Join-Path $Store $Name
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

function Install-NHRequestedRelease([string]$Original, [string]$Current, [string]$Store, [string]$Request,
    [string]$StatusPath, [string]$CodeRoot = '') {
    $wanted = Read-NHSmallUpdateJson $Request
    if ($wanted.version -isnot [string] -or $null -eq (ConvertTo-NHReleaseVersion $wanted.version)) {
        throw 'Invalid requested update version.'
    }
    $channel = Get-UpdateRequestChannel $wanted
    # A release replaces its own track's runtime. An empty beta track must still
    # be newer than stable; an empty stable track (a beta package) takes any stable.
    $track = Get-NHTrackRuntime $Original $channel
    if ($track) { $Current = $track } elseif ($channel -ceq 'stable') { $Current = '' }
    if ($Current -and (ConvertTo-NHReleaseVersion $wanted.version) -le (Get-NHProductVersion $Current)) { return $Current }
    # A launcher handed over from an older original runs its own update worker.
    $workerRoot = $Original
    if ($CodeRoot) { $workerRoot = $CodeRoot }
    $worker = Join-Path $workerRoot 'Prepare-NHReleaseUpdate.ps1'
    Assert-PlainPath $worker
    $prepared = (& $worker -Store $Store -Request $Request -StatusPath $StatusPath) | ConvertFrom-Json
    if ($prepared.version -cne $wanted.version -or $prepared.sha256 -cne $wanted.sha256) {
        throw 'Prepared update differs from the accepted release.'
    }
    $selection = [ordered]@{ format = 1; version = $prepared.version; sha256 = $prepared.sha256 }
    $updated = Get-NHSelectedUpdateRuntime $Store $selection
    Test-NHUpdatedClient $updated
    Set-NHUpdateSelection $Store $selection (Get-NHTrackSelectionName $channel)
    return $updated
}

function Get-NHPackageChannel([string]$Runtime) {
    # The release track a package was published on. Packages without the
    # marker predate it and are stable; any doubt is stable too.
    try {
        $path = Join-Path $Runtime 'BUILD-IDENTITY.json'
        Assert-PlainPath $path
        $file = New-Object IO.FileInfo($path)
        if (-not $file.Exists -or $file.Length -gt 2097152) { return 'stable' }
        $identity = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8) | ConvertFrom-Json
        $marker = $identity.PSObject.Properties['release_channel']
        if ($null -ne $marker -and $marker.Value -ceq 'beta') { return 'beta' }
    } catch { }
    return 'stable'
}

function Get-NHTrackRuntime([string]$Original, [string]$Channel) {
    # A track plays its selected update, else the original installation when it
    # was published on that track. The other track starts empty.
    $name = Get-NHTrackSelectionName $Channel
    $fallback = $null
    if ((Get-NHPackageChannel $Original) -ceq $Channel) { $fallback = $Original }
    try {
        Assert-PlainPath $Original
        if (-not (Test-Path -LiteralPath (Join-Path $Original 'BUILD-IDENTITY.json') -PathType Leaf)) { return $fallback }
        $store = Get-NHUpdateStore $Original
        Assert-PlainPath $store
        $selection = Join-Path $store $name
        if (Test-Path -LiteralPath $selection -PathType Leaf) {
            return Get-NHSelectedUpdateRuntime $store (Read-NHSmallUpdateJson $selection)
        }
    } catch {
        [Console]::Error.WriteLine('New Horizons: The selected ' + $Channel + ' update is unavailable.')
    }
    return $fallback
}

function Select-NHPortableRelease([string]$Original, [bool]$ApplyRequest = $false) {
    # Read-only selection of the original's own track. Retired in-game request.json
    # files are never consumed.
    return [PSCustomObject]@{ runtime = (Get-NHTrackRuntime $Original (Get-NHPackageChannel $Original)); request = $null }
}

# The selected package's launcher prints this token when its code loads.
$script:NHLauncherSelfTestToken = 'NH_WINDOWS_PORTABLE_LAUNCHER_V2'

# A cold Windows PowerShell start with antivirus scans can take many seconds.
function Test-NHLauncherSelfTest([string]$Runtime, [int]$TimeoutMilliseconds = 30000) {
    $operation = Join-Path $Runtime 'Player-Launcher-Operation.ps1'
    Assert-PlainPath $operation
    if (-not [IO.File]::Exists($operation)) { return $false }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = Join-Path $PSHOME 'powershell.exe'
    $info.WorkingDirectory = $Runtime
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $all = @('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$operation,'-Operation','SelfTest')
    $info.Arguments = (@($all | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    foreach ($name in @('NH_PORTABLE_UPDATE_REQUEST','NH_LAUNCHER_READY_EVENT','NH_WINDOWS_PORTABLE_UPDATE_CHILD',
                        'NH_WINDOWS_LAUNCHER_DELEGATED','NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1','NH_WINDOWS_BOOTSTRAP_UI')) {
        $info.EnvironmentVariables.Remove($name)
    }
    $process = [Diagnostics.Process]::Start($info)
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $null = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMilliseconds)) {
            try { $process.Kill() } catch { } # It may exit between the wait and the kill.
            $null = $process.WaitForExit(5000)
            Write-UpdateLog ('Launcher self-test of ' + $Runtime + ' timed out after ' + $TimeoutMilliseconds + ' ms')
            return $false
        }
        $process.WaitForExit()
        $passed = $process.ExitCode -eq 0 -and $output.Wait(5000) -and $output.Result.Trim() -ceq $script:NHLauncherSelfTestToken
        Write-UpdateLog ('Launcher self-test of ' + $Runtime + ' exited with code ' + $process.ExitCode + ' in ' +
            $clock.ElapsedMilliseconds + ' ms; ' + $(if ($passed) { 'passed' } else { 'failed' }))
        return $passed
    } finally {
        $process.Dispose()
    }
}

function Get-NHNewerLauncher([string]$Original) {
    # Hand over to the launcher inside the selected update of the original's own
    # track, so launcher improvements arrive with updates. Any doubt keeps this
    # launcher; an older release on the other track never takes over.
    try {
        $runtime = (Select-NHPortableRelease $Original $false).runtime
        if ($runtime -ieq $Original -or (Get-NHProductVersion $runtime) -le (Get-NHProductVersion $Original)) { return $null }
        $entry = Join-Path $runtime 'Start-New-Horizons.ps1'
        Assert-PlainPath $entry
        if (-not [IO.File]::Exists($entry) -or -not (Test-NHLauncherSelfTest $runtime)) { return $null }
        Write-UpdateLog ('Handing over to the launcher of the selected update ' + (Get-NHProductVersionText $runtime))
        return $entry
    } catch {
        Write-UpdateLog 'The selected update launcher is unavailable; keeping this launcher' $_
        return $null
    }
}

function Resolve-NHLauncherOriginal([string]$PackageRoot, [string]$Delegated) {
    # A handed-over launcher manages the original installation's update store.
    if ([string]::IsNullOrEmpty($Delegated)) { return $PackageRoot }
    if (-not [IO.Path]::IsPathRooted($Delegated)) { throw 'Invalid handed-over launcher installation.' }
    $original = [IO.Path]::GetFullPath($Delegated)
    Assert-PlainPath $original
    if ($original -ieq $PackageRoot -or (Select-NHPortableRelease $original $false).runtime -ine $PackageRoot) {
        throw 'The handed-over launcher is not the selected update of its installation.'
    }
    return $original
}
