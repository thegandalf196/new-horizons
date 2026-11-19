# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. This file contains no launch-time side effects.
function Read-BootstrapArguments {
    $raw = [Environment]::GetEnvironmentVariable('NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1', 'Process')
    [Environment]::SetEnvironmentVariable('NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1', $null, 'Process')
    if ([string]::IsNullOrEmpty($raw)) { return @() }
    $value = $raw | ConvertFrom-Json
    if ($null -eq $value -or $value.version -ne 1 -or
        $value.arguments -isnot [Array] -or
        (@($value.PSObject.Properties.Name | Sort-Object) -join ',') -cne 'arguments,version') {
        throw 'Invalid portable launcher argument envelope.'
    }
    foreach ($argument in $value.arguments) {
        if ($argument -isnot [string] -or $argument.Contains([char]0)) {
            throw 'Invalid portable launcher argument.'
        }
        # No PowerShell parsing or interpolation of argument contents.
        Write-Output -NoEnumerate $argument
    }
}

function ConvertTo-NativeArgument([AllowEmptyString()][string]$Value) {
    # Microsoft C runtime argv quoting, including empty arguments and trailing \.
    $quoted = New-Object Text.StringBuilder
    $null = $quoted.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') {
            $null = $quoted.Append(('\' * (2 * $slashes + 1)))
        } else {
            $null = $quoted.Append(('\' * $slashes))
        }
        $null = $quoted.Append($character)
        $slashes = 0
    }
    $null = $quoted.Append(('\' * (2 * $slashes))).Append('"')
    return $quoted.ToString()
}

# Only complete the exact newly assigned session after its owned child is reaped.
function Complete-NativeClientSession([string]$Session, [int]$ExitCode) {
    if (-not $Session -or -not [IO.Directory]::Exists($Session) -or
        -not [IO.File]::Exists((Join-Path $Session 'active.lock'))) { return }
    if (([IO.File]::GetAttributes((Join-Path $Session 'active.lock')) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { return }
    $cursor = [IO.Path]::GetFullPath($Session)
    while ($cursor) {
        if (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { return }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    $status = [BitConverter]::ToUInt32([BitConverter]::GetBytes($ExitCode), 0)
    $cancelled = $status -eq 3221225786L # STATUS_CONTROL_C_EXIT, not an access violation.
    $receipts = [ordered]@{}
    if ($cancelled) { $receipts['launcher-cancelled'] = "NH_LAUNCH_CANCEL_V1`n" }
    $receipts['launcher-exit'] = "NH_LAUNCH_EXIT_V1`n$ExitCode`n"
    foreach ($name in $receipts.Keys) {
        $destination = Join-Path $Session $name
        if ([IO.File]::Exists($destination)) { continue }
        $temporary = Join-Path $Session ('.launcher-' + [Guid]::NewGuid().ToString('N'))
        $owned = $false
        try {
            $stream = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $owned = $true
            try {
                $bytes = [Text.Encoding]::ASCII.GetBytes($receipts[$name])
                $stream.Write($bytes, 0, $bytes.Length)
                $stream.Flush($true)
            } finally { $stream.Dispose() }
            [IO.File]::Move($temporary, $destination)
            $owned = $false
        } finally {
            if ($owned -and [IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
        }
    }
    $intent = Join-Path $Session 'launcher-cancelled'
    $terminal = Join-Path $Session 'launcher-exit'
    if ($cancelled -and [IO.File]::Exists($intent) -and [IO.File]::Exists($terminal)) {
        foreach ($receipt in @($intent, $terminal)) {
            if (([IO.File]::GetAttributes($receipt) -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                ([IO.FileInfo]$receipt).Length -gt 128) { return }
        }
        if ([IO.File]::ReadAllText($intent) -ceq "NH_LAUNCH_CANCEL_V1`n" -and
            [IO.File]::ReadAllText($terminal) -ceq "NH_LAUNCH_EXIT_V1`n$ExitCode`n") {
            [IO.File]::Delete((Join-Path $Session 'pending'))
        }
    }
}

function Test-NativeClientGameWindow($Process) {
    try {
        $Process.Refresh()
        # SDL2 and SDL3 both create the player's window with this exact title.
        # The handle and title come from the owned child, never the launcher.
        return $Process.MainWindowHandle -ne [IntPtr]::Zero -and $Process.MainWindowTitle -ceq 'New Horizons'
    } catch {
        # Exit can race a presentation probe; the owned wait decides completion.
        return $false
    }
}

function New-NativeClientReadySignal {
    $name = 'Local\NewHorizonsLauncherReady-' + [Guid]::NewGuid().ToString('N')
    $created = $false
    $handle = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $name, [ref]$created)
    if (-not $created) { $handle.Dispose(); throw 'The game-ready event was not exclusive.' }
    return [PSCustomObject]@{ Name = $name; Handle = $handle }
}

function Wait-NativeClientExit($Process, $StartupProgress, $ReadySignal = $null) {
    if ($null -ne $StartupProgress) {
        try {
            while (-not $Process.WaitForExit(100)) {
                try {
                    if ($null -ne $ReadySignal) {
                        if ($ReadySignal.Handle.WaitOne(0)) { break }
                    } elseif (Test-NativeClientGameWindow $Process) { break }
                    Update-GameStartupProgress $StartupProgress
                } catch {
                    # A failed optional window must not abandon the child wait.
                    break
                }
            }
        } finally {
            Close-AssetImportProgress $StartupProgress
        }
    }
    $Process.WaitForExit()
}

function Invoke-NativeClient([string]$Client, [string]$WorkingDirectory,
    [AllowEmptyCollection()][string[]]$Arguments = @(), $StartupProgress = $null) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Client
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.Arguments = (@($Arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    $commandLength = (ConvertTo-NativeArgument $Client).Length + 1 + $info.Arguments.Length
    if ($commandLength -ge 32767) { throw 'Game arguments exceed the Windows command length.' }
    $process = New-Object Diagnostics.Process
    $readySignal = $null
    $info.EnvironmentVariables.Remove('NH_LAUNCHER_READY_EVENT')
    try {
        if ($null -ne $StartupProgress -and $env:NH_LAUNCHER_MANAGES_UPDATES -ceq '1') {
            $readySignal = New-NativeClientReadySignal
            $info.EnvironmentVariables['NH_LAUNCHER_READY_EVENT'] = $readySignal.Name
        }
        $process.StartInfo = $info
        if (-not $process.Start()) { throw 'Windows could not start the client.' }
        Wait-NativeClientExit $process $StartupProgress $readySignal
        return [int]$process.ExitCode
    } finally {
        if ($null -ne $StartupProgress) { Close-AssetImportProgress $StartupProgress }
        $process.Dispose()
        if ($null -ne $readySignal) { $readySignal.Handle.Dispose() }
    }
}
