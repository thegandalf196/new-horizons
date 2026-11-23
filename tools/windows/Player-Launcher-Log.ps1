# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Keep this file ASCII.
# Plain-text launcher session log, shared by the launcher window and its owned
# background children. Each line has the local time with milliseconds, the
# seconds since that process opened the log and the writing role. Logging never
# changes launcher behaviour: every failure only stops logging. Personal folders
# are written as environment variables and the log never records tokens, URL
# queries or the environment.
# Reloading this file in the same script scope keeps the open session.
if ($null -eq (Get-Variable -Name NHLogPath -Scope Script -ErrorAction SilentlyContinue)) {
    $script:NHLogPath = ''
    $script:NHLogRole = 'launcher'
    $script:NHLogClock = $null
    $script:NHLogFull = $false
    $script:NHLogRedactions = @()
}
$script:NHLogMaximumBytes = 1048576
$script:NHLogName = 'launcher.log'
$script:NHLogPreviousName = 'launcher.previous.log'
# The window keeps one session across a hand-over to the selected update's launcher.
$script:NHLogSessionVariable = 'NH_LAUNCHER_LOG_SESSION'
# Update-check phases the child reports through its status file: the text the
# window shows, and how a timeout names the step that was still running.
$script:NHCheckPhases = @{
    launch = @('Starting the update check...', 'starting PowerShell for the update check')
    start = @('Starting the update check...', 'starting the update check')
    request = @('Contacting GitHub...', 'contacting GitHub')
    release = @('Checking the published release...', 'checking the published release')
    notes = @('Reading release notes...', 'reading release notes')
}

function Set-NHLogRedactions {
    # Longest first, so a profile subfolder is not reported as a partial profile path.
    $pairs = @()
    foreach ($entry in @(@([IO.Path]::GetTempPath(), '%TEMP%'), @($env:TEMP, '%TEMP%'),
                         @($env:LOCALAPPDATA, '%LOCALAPPDATA%'), @($env:APPDATA, '%APPDATA%'),
                         @($env:USERPROFILE, '%USERPROFILE%'))) {
        $value = [string]$entry[0]
        if ($value.Length -lt 4) { continue }
        # Also the expanded form: Windows may report a folder by its 8.3 short name.
        $forms = @($value)
        try { $forms += [IO.Path]::GetFullPath($value) } catch { }
        foreach ($form in ($forms | Select-Object -Unique)) {
            $pairs += [PSCustomObject]@{Value=$form.TrimEnd('\', '/'); Name=$entry[1]}
        }
    }
    $script:NHLogRedactions = @($pairs | Sort-Object { $_.Value.Length } -Descending)
}

function Protect-NHLogText([string]$Text) {
    # One log line per entry; personal folders and the user name in paths are replaced.
    if (-not $Text) { return '' }
    $Text = $Text.Replace("`r`n", ' | ').Replace("`n", ' | ').Replace("`r", ' | ')
    foreach ($pair in $script:NHLogRedactions) {
        $Text = [regex]::Replace($Text, [regex]::Escape($pair.Value), $pair.Name.Replace('$', '$$'), 'IgnoreCase')
    }
    $user = [Environment]::UserName
    if ($user -and $user.Length -ge 3) {
        # Also the 8.3 short form, such as C:\Users\JOHNDO~1, of a long user name.
        $short = ($user -replace '[^A-Za-z0-9]', '')
        $names = [regex]::Escape($user)
        if ($short.Length -ge 3) { $names += '|' + [regex]::Escape($short.Substring(0, [Math]::Min(6, $short.Length))) + '~[0-9]+' }
        $Text = [regex]::Replace($Text, '(?<=[\\/])(?:' + $names + ')(?![A-Za-z0-9_.-])', '%USERNAME%', 'IgnoreCase')
    }
    return $Text
}

function Open-NHLauncherLog([string]$Path, [string]$Role, [bool]$Rotate = $false) {
    # A child appends to the session the window started; only the window rotates.
    $script:NHLogPath = ''
    if (-not $Path) { return }
    try {
        Set-NHLogRedactions
        $full = [IO.Path]::GetFullPath($Path)
        if ([IO.Path]::GetFileName($full) -cne $script:NHLogName) { return }
        $directory = [IO.Path]::GetDirectoryName($full)
        Assert-PlainPath $full
        $null = [IO.Directory]::CreateDirectory($directory)
        if ($Rotate -and [IO.File]::Exists($full)) {
            $previous = Join-Path $directory $script:NHLogPreviousName
            try {
                if ([IO.File]::Exists($previous)) { [IO.File]::Delete($previous) }
                [IO.File]::Move($full, $previous)
            } catch { } # Another launcher may still hold it; this session appends.
        }
        $script:NHLogRole = $Role
        $script:NHLogClock = [Diagnostics.Stopwatch]::StartNew()
        $script:NHLogFull = $false
        $script:NHLogPath = $full
    } catch { $script:NHLogPath = '' }
}

function Write-NHLauncherLog([string]$Message) {
    if (-not $script:NHLogPath) { return }
    try {
        $culture = [Globalization.CultureInfo]::InvariantCulture
        $prefix = [string]::Format($culture, '{0} +{1:0.000}s {2,-8} ',
            [DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:ss.fff zzz', $culture),
            $script:NHLogClock.Elapsed.TotalSeconds, $script:NHLogRole)
        $text = Protect-NHLogText $Message
        $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
        $stream = [IO.File]::Open($script:NHLogPath, [IO.FileMode]::Append, [IO.FileAccess]::Write, $share)
        try {
            if ($stream.Length -ge $script:NHLogMaximumBytes) {
                if ($script:NHLogFull) { return }
                $script:NHLogFull = $true
                $text = 'Log size limit reached; later lines are omitted.'
            }
            # One write per line keeps concurrent parent and child lines whole.
            $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes($prefix + $text + "`r`n")
            $stream.Write($bytes, 0, $bytes.Length)
        } finally { $stream.Dispose() }
    } catch { } # Logging is diagnostic only.
}

function Format-NHLogError($Failure) {
    # Exception type and message with its inner exceptions, network status and HTTP code.
    $exception = $Failure
    if ($exception -is [Management.Automation.ErrorRecord]) { $exception = $exception.Exception }
    $parts = @()
    for ($depth = 0; $null -ne $exception -and $depth -lt 5; $depth++) {
        $text = $exception.GetType().FullName + ': ' + $exception.Message
        if ($exception -is [Net.WebException]) {
            $text += ' [network status ' + $exception.Status + ']'
            if ($exception.Response -is [Net.HttpWebResponse]) { $text += ' [HTTP ' + [int]$exception.Response.StatusCode + ']' }
        }
        $parts += $text
        $exception = $exception.InnerException
    }
    return ($parts -join ' <- inner ')
}

function Get-NHLauncherLogHint {
    # The status line names the log so a player can open or send it.
    if (-not $script:NHLogPath) { return '' }
    return ' Details: ' + (Protect-NHLogText $script:NHLogPath)
}

function Get-NHLauncherLogPath {
    # The stable profile's game logs folder; one window serves both tracks.
    $local = [Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process')
    if ([string]::IsNullOrWhiteSpace($local) -or -not [IO.Path]::IsPathRooted($local)) { return '' }
    return Join-Path (Join-Path (Join-Path ([IO.Path]::GetFullPath($local)) 'HeroesIII-NewHorizons') 'logs') $script:NHLogName
}

function Write-NHLauncherLogHeader([string]$Code) {
    $version = 'unknown'
    try { $version = Get-NHProductVersionText $Code } catch { }
    Write-NHLauncherLog ('New Horizons launcher ' + $version + ' (Windows), code ' + $Code + ', pid ' + $PID)
    $bits = if ([Environment]::Is64BitOperatingSystem) { '64-bit' } else { '32-bit' }
    Write-NHLauncherLog ('Windows: ' + [Environment]::OSVersion.VersionString + ' ' + $bits)
    $release = 'unknown'
    try {
        $key = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full' -Name Release -ErrorAction Stop
        $release = [string]$key.Release
    } catch { }
    Write-NHLauncherLog ('PowerShell ' + $PSVersionTable.PSVersion + ', CLR ' + [Environment]::Version + ', .NET Framework 4 release ' + $release)
}

function Start-NHLauncherSession([string]$Code) {
    # Started once by the window's entry point; a handed-over launcher continues it.
    if ($script:NHLogPath) { return }
    $path = Get-NHLauncherLogPath
    if (-not $path) { return }
    $continued = [Environment]::GetEnvironmentVariable($script:NHLogSessionVariable, 'Process') -ceq $path
    Open-NHLauncherLog $path 'launcher' (-not $continued)
    if (-not $script:NHLogPath) { return }
    [Environment]::SetEnvironmentVariable($script:NHLogSessionVariable, $path, 'Process')
    if ($continued) { Write-NHLauncherLog ('Continuing in the launcher of the selected update: ' + $Code) }
    Write-NHLauncherLogHeader $Code
}

function Set-NHCheckPhase([string]$StatusPath, [string]$Phase) {
    # The parent shows the phase text and names it if its deadline stops this child.
    Write-NHLauncherLog ('Phase: ' + $Phase)
    if (-not $StatusPath) { return }
    try { [IO.File]::WriteAllText($StatusPath, 'phase:' + $Phase) } catch { }
}

function Get-NHCheckPhaseText([string]$Phase, [int]$Index) {
    if (-not $script:NHCheckPhases.ContainsKey($Phase)) { $Phase = 'launch' }
    return $script:NHCheckPhases[$Phase][$Index]
}
