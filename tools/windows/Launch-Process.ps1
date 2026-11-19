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
