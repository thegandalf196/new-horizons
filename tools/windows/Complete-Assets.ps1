# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Resolve only the named Data child, never arbitrary ancestors.
function Test-CompleteAssetShape([string]$Root) {
    foreach ($folder in @('Data', 'Maps', 'Mp3')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $folder) -PathType Container)) { return $false }
    }
    foreach ($name in @('H3bitmap.lod', 'H3sprite.lod', 'H3ab_bmp.lod', 'H3ab_spr.lod')) {
        $path = Join-Path $Root ('Data\' + $name)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
        if ((Get-Item -LiteralPath $path -Force).Length -eq 0) { return $false }
    }
    return $true
}

function Resolve-CompleteAssetRoot([string]$Selected) {
    Assert-PlainPath $Selected
    $item = Get-Item -LiteralPath $Selected -Force
    if (-not $item.PSIsContainer) { throw 'Select an ordinary Complete installation or its Data folder.' }
    $root = $item.FullName
    # A Complete root may itself be named Data; prefer its own full layout.
    if ($item.Name -ieq 'Data' -and -not (Test-CompleteAssetShape $root)) {
        if ($null -eq $item.Parent) { throw 'Data must belong to a complete Heroes III installation.' }
        $root = $item.Parent.FullName
    }
    Assert-CompleteLayout $root
    return $root
}
