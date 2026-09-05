# Heroes III: New Horizons - isolated Windows asset setup and player launch
# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Keep this script ASCII (or use UTF-8 with BOM).
[CmdletBinding()]
param(
    [string]$AssetsPath,
    [switch]$SelectAssets,
    [switch]$SetupOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$profileRoot = $null
$stage = $null
$lock = $null

function Assert-PlainPath([string]$Path) {
    # Reject existing junctions/symlinks, including ancestors. Never traverse them.
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Linked folders/files are not supported: $cursor. Choose an ordinary local folder."
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

function Get-SafeChildPath([string]$Root, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or
        $Relative.Contains(':') -or (($Relative -split '[\\/]') -contains '..')) {
        throw 'Invalid relative asset path. Select the Complete installation again.'
    }
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $result = [IO.Path]::GetFullPath([IO.Path]::Combine($Root, $Relative))
    if (-not $result.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Asset path escapes its content folder.'
    }
    # VCMIDirsWIN32 and Windows PowerShell 5.1 are not a long-path contract.
    if ($result.Length -ge 260) {
        throw "Path is too long: $result. Use a shorter local installation/profile path."
    }
    return $result
}

function Assert-CompleteLayout([string]$Root) {
    Assert-PlainPath $Root
    foreach ($folder in @('Data', 'Maps', 'Mp3')) {
        $path = Get-SafeChildPath $Root $folder
        Assert-PlainPath $path
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Missing $folder folder in $Root. Select the installed Heroes III Complete folder, not its Data subfolder, a ZIP, or a GOG installer."
        }
    }
    # These are VCMI's SoD/base and Armageddon's Blade archive names.
    foreach ($archive in @('Data\H3bitmap.lod', 'Data\H3sprite.lod', 'Data\H3ab_bmp.lod', 'Data\H3ab_spr.lod')) {
        $path = Get-SafeChildPath $Root $archive
        Assert-PlainPath $path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            throw "Missing or empty Complete archive: $archive. Supply an intact, unmodified Complete installation. Heroes III HD Edition is unsupported."
        }
    }
}

function Get-AssetInventory([string]$Root) {
    Assert-CompleteLayout $Root
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    foreach ($folder in @('Data', 'Maps', 'Mp3')) {
        $pending.Push((Get-SafeChildPath $Root $folder))
    }
    while ($pending.Count -gt 0) {
        foreach ($item in @(Get-ChildItem -LiteralPath ($pending.Pop()) -Force)) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Linked asset is unsupported: $($item.FullName). Original folders are only read, never linked or changed."
            }
            if ($item.PSIsContainer) {
                $pending.Push($item.FullName)
            } else {
                $relative = $item.FullName.Substring($prefix.Length)
                $null = Get-SafeChildPath $Root $relative
                [PSCustomObject]@{ path = $relative; length = [long]$item.Length }
            }
        }
    }
}

function Assert-ReadyContent([string]$Root) {
    Assert-CompleteLayout $Root
    $marker = Get-SafeChildPath $Root '.nh-assets.json'
    Assert-PlainPath $marker
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw 'No completed import record. Reselect your Complete folder to make a verified private copy.'
    }
    $manifest = [IO.File]::ReadAllText($marker, [Text.Encoding]::UTF8) | ConvertFrom-Json
    if ($manifest.format -ne 1 -or $manifest.product -ne 'HeroesIII-NewHorizons' -or $manifest.complete -ne $true) {
        throw 'Invalid import record. Reselect your Complete folder.'
    }
    $files = @($manifest.files)
    if ($files.Count -lt 4) { throw 'Incomplete import record. Reselect your Complete folder.' }
    foreach ($file in $files) {
        if (($file.path -split '[\\/]')[0] -notin @('Data', 'Maps', 'Mp3')) {
            throw 'Unexpected import record path. Reselect your Complete folder.'
        }
        $path = Get-SafeChildPath $Root $file.path
        Assert-PlainPath $path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $file.length) {
            throw "Imported file missing or changed in size: $($file.path). Reselect your Complete folder."
        }
    }
    # Full hashes are verified during import. Launch checks the manifest and sizes,
    # not multi-gigabyte hashes on every play. Extra generated maps are allowed.
}

function Select-CompleteFolder {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select your installed, legitimate Heroes III Complete folder (containing Data, Maps and Mp3). Files will be copied, not modified.'
    $dialog.ShowNewFolderButton = $false
    try {
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            return $dialog.SelectedPath
        }
        return $null
    } finally {
        $dialog.Dispose()
    }
}

try {
    $packageRoot = [IO.Path]::GetFullPath($PSScriptRoot)
    $client = Join-Path $packageRoot 'VCMI_client.exe'
    foreach ($required in @('VCMI_client.exe', 'VCMI_lib.dll', 'config\dirs.json', 'config\filesystem.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $required) -PathType Leaf)) {
            throw "Incomplete download/extraction: missing $required. Extract the entire New Horizons Windows ZIP, then use Play-New-Horizons.cmd inside it."
        }
    }
    $localData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process')
    if ([string]::IsNullOrWhiteSpace($localData) -or -not [IO.Path]::IsPathRooted($localData)) {
        throw 'LOCALAPPDATA is unavailable. Run as your normal Windows user; do not run as administrator.'
    }
    $profileRoot = [IO.Path]::GetFullPath((Join-Path $localData 'HeroesIII-NewHorizons'))
    $content = Join-Path $profileRoot 'content'
    # Refuse mismatched profiles rather than silently writing one and launching another.
    $dirs = [IO.File]::ReadAllText((Join-Path $packageRoot 'config\dirs.json'), [Text.Encoding]::UTF8) | ConvertFrom-Json
    $expected = @{
        userDataPath = 'content'; userConfigPath = 'config'; userCachePath = 'cache'
        userLogsPath = 'logs'; userSavePath = 'Saves'
    }
    foreach ($key in $expected.Keys) {
        $value = '%LOCALAPPDATA%\HeroesIII-NewHorizons\' + $expected[$key]
        if ($dirs.$key -cne $value) {
            throw "Unexpected $key in config\dirs.json. Restore that file from this release; setup will not overwrite a custom profile."
        }
        $path = Get-SafeChildPath $profileRoot $expected[$key]
        Assert-PlainPath $path
    }
    Assert-PlainPath $profileRoot
    $null = [IO.Directory]::CreateDirectory($profileRoot)
    $lockPath = Join-Path $profileRoot '.setup.lock'
    Assert-PlainPath $lockPath
    try {
        $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch {
        throw "NH profile is busy or not writable: $profileRoot. Close other New Horizons sessions, check free space and your user-folder permissions, then retry without elevation."
    }
    foreach ($folder in @('config', 'cache', 'logs', 'Saves')) {
        $null = [IO.Directory]::CreateDirectory((Join-Path $profileRoot $folder))
    }

    $ready = $false
    try {
        Assert-ReadyContent $content
        $ready = $true
    } catch {
        Write-Host ('Asset setup needed: ' + $_.Exception.Message)
    }
    if (-not $ready -or $SelectAssets -or -not [string]::IsNullOrWhiteSpace($AssetsPath)) {
        Write-Host 'First import needs space for a full copy of Data, Maps and Mp3.'
        Write-Host 'Reselection needs another full copy; the last-good copy and Saves are retained.'
        $source = $AssetsPath
        if ([string]::IsNullOrWhiteSpace($source)) { $source = Select-CompleteFolder }
        if ([string]::IsNullOrWhiteSpace($source)) {
            Write-Host 'Setup cancelled. Existing content and saves were not replaced. No game was started.'
            exit 0
        }
        $source = (Get-Item -LiteralPath $source -Force).FullName
        Assert-PlainPath $source
        $sourcePrefix = $source.TrimEnd('\') + '\'
        $profilePrefix = $profileRoot.TrimEnd('\') + '\'
        if ($profilePrefix.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase) -or
            $sourcePrefix.StartsWith($profilePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Choose the original Complete installation outside the NH profile, not the NH content folder or its parent.'
        }
        $inventory = @(Get-AssetInventory $source)
        if (@($inventory | Where-Object { $_.path -like 'Maps\*.h3m' }).Count -eq 0 -or
            @($inventory | Where-Object { $_.path -like 'Mp3\*.mp3' }).Count -eq 0) {
            throw 'Complete installation needs Maps (.h3m) and music (.mp3). Restore missing original files before importing.'
        }
        $bytes = ($inventory | Measure-Object -Property length -Sum).Sum
        Write-Host ('Importing {0} files ({1:N2} GiB); copying and verifying may take several minutes.' -f $inventory.Count, ($bytes / 1GB))
        $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($profileRoot))
        if ($drive.AvailableFreeSpace -lt ($bytes + 64MB)) {
            throw 'Not enough free space for a staged asset copy plus working space. Free space on the NH profile drive and retry; existing content is unchanged.'
        }
        $stage = Join-Path $profileRoot ('.import-' + [Guid]::NewGuid().ToString('N'))
        $null = [IO.Directory]::CreateDirectory($stage)
        foreach ($folder in @('Data', 'Maps', 'Mp3')) {
            $null = [IO.Directory]::CreateDirectory((Join-Path $stage $folder))
        }
        $completed = 0
        foreach ($file in $inventory) {
            $from = Get-SafeChildPath $source $file.path
            $to = Get-SafeChildPath $stage $file.path
            Assert-PlainPath $from
            $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to))
            [IO.File]::Copy($from, $to, $false)
            if ((Get-Item -LiteralPath $from).Length -ne $file.length -or
                (Get-Item -LiteralPath $to).Length -ne $file.length -or
                (Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash) {
                throw "Copy verification failed for $($file.path). Stop other programs modifying the source, check the disk, then retry."
            }
            $completed++
            Write-Progress -Activity 'Copying and verifying Complete assets' -Status "$completed / $($inventory.Count)" -PercentComplete (100 * $completed / $inventory.Count)
        }
        Write-Progress -Activity 'Copying and verifying Complete assets' -Completed
        Assert-CompleteLayout $stage
        $manifest = [ordered]@{ format = 1; product = 'HeroesIII-NewHorizons'; complete = $true; files = $inventory }
        $json = $manifest | ConvertTo-Json -Depth 5
        [IO.File]::WriteAllText((Join-Path $stage '.nh-assets.json'), $json, (New-Object Text.UTF8Encoding $false))
        Assert-ReadyContent $stage

        # Rename only after successful copy AND validation. Never merge into live content.
        $backup = $null
        if (Test-Path -LiteralPath $content) {
            Assert-PlainPath $content
            $backup = Join-Path $profileRoot ('content.previous-' + [Guid]::NewGuid().ToString('N'))
            [IO.Directory]::Move($content, $backup)
        }
        try {
            [IO.Directory]::Move($stage, $content)
            $stage = $null
        } catch {
            $activationError = $_.Exception.Message
            if ($backup) {
                try { [IO.Directory]::Move($backup, $content) }
                catch { throw "Activation failed ($activationError). Previous content is safe at $backup; restore it to content while the game is closed. Saves were not moved." }
            }
            throw "Could not activate the verified copy: $activationError. Existing content was restored; close the game and retry."
        }
        if ($backup) { Write-Host "Previous content retained at: $backup" }
    }
    Assert-ReadyContent $content
    if ($SetupOnly) {
        Write-Host 'Verified private assets are ready. No game was started.'
        exit 0
    }
    Write-Host "Starting New Horizons. Saves and logs: $profileRoot"
    # No shell-evaluated selected path and no administrator verb. Hold profile lock until exit.
    $game = Start-Process -FilePath $client -WorkingDirectory $packageRoot -PassThru -Wait
    $game.Refresh()
    if ($game.ExitCode -ne 0) {
        throw "VCMI_client.exe exited with code $($game.ExitCode). Check $profileRoot\logs. If a DLL is missing, re-extract the full matching Windows package."
    }
} catch {
    [Console]::Error.WriteLine('New Horizons: ' + $_.Exception.Message)
    [Console]::Error.WriteLine('Setup failures prevent launch. Check free space/permissions and README-New-Horizons.txt. Do not use administrator mode.')
    exit 1
} finally {
    if ($stage -and (Test-Path -LiteralPath $stage)) {
        # Only this invocation's unique, never-activated staging directory is eligible.
        try { Remove-Item -LiteralPath $stage -Recurse -Force }
        catch { [Console]::Error.WriteLine("Incomplete staging copy remains at $stage. It is not active; remove it later with the game closed.") }
    }
    if ($lock) { $lock.Dispose() }
}
