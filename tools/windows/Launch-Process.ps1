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

function Invoke-NativeClient([string]$Client, [string]$WorkingDirectory,
    [AllowEmptyCollection()][string[]]$Arguments = @()) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Client
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.Arguments = (@($Arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    $commandLength = (ConvertTo-NativeArgument $Client).Length + 1 + $info.Arguments.Length
    if ($commandLength -ge 32767) { throw 'Game arguments exceed the Windows command length.' }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'Windows could not start the client.' }
        $process.WaitForExit()
        return [int]$process.ExitCode
    } finally {
        $process.Dispose()
    }
}
