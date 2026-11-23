# SPDX-License-Identifier: GPL-2.0-or-later
# Windows PowerShell 5.1. Keep this file ASCII.
# Plain-text release notes for the launcher's What's new window. Release bodies
# come from the network and are only ever text: Markdown is flattened, links
# lose their targets and each note is capped. Offline, the installed version's
# notes come from the changelog bundled with it.
$script:NHNotesBundledName = 'CHANGELOG-New-Horizons.md'
$script:NHNotesMaximumChars = 20000
$script:NHNotesMaximumInput = 200000
$script:NHNotesShortened = "`n`n[Release notes shortened.]"
$script:NHNotesOffline = 'Release notes are not available offline.'
$script:NHNotesMissing = 'No release notes are available for this version.'
$script:NHNotesVersionPattern = '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
# Control, zero-width and bidirectional formatting characters never reach the window.
$script:NHNotesUnsafe = '[\x00-\x08\x0B-\x1F\x7F-\x9F\u200B-\u200F\u202A-\u202E\u2060-\u2069\uFEFF]'

function Update-NHNotesText([string]$Text, [string]$Pattern, $Replacement, [string]$Options = 'Multiline') {
    # Every span excludes its own opening delimiter; the timeout is a second bound.
    $timeout = [TimeSpan]::FromSeconds(2)
    $flags = [Text.RegularExpressions.RegexOptions]$Options
    if ($Replacement -is [scriptblock]) {
        return [regex]::Replace($Text, $Pattern, [Text.RegularExpressions.MatchEvaluator]$Replacement, $flags, $timeout)
    }
    return [regex]::Replace($Text, $Pattern, [string]$Replacement, $flags, $timeout)
}

function Limit-NHReleaseNotes([string]$Text) {
    $limit = $script:NHNotesMaximumChars
    if ($Text.Length -le $limit) { return $Text }
    $cut = $Text.LastIndexOf([char]10, $limit - 1)
    if ($cut -le [int]($limit / 2)) { $cut = $limit }
    return $Text.Substring(0, $cut).TrimEnd() + $script:NHNotesShortened
}

function ConvertTo-NHPlainReleaseNotes($Markdown) {
    # Markdown or HTML-ish text as plain text, or $null when there is none.
    if ($Markdown -isnot [string]) { return $null }
    $text = $Markdown
    if ($text.Length -gt $script:NHNotesMaximumInput) { $text = $text.Substring(0, $script:NHNotesMaximumInput) }
    $text = $text.Replace("`r`n", "`n").Replace("`r", "`n").Replace("`t", '    ')
    $text = Update-NHNotesText $text '<!--.*?(?:-->|\z)' '' 'Singleline'
    $text = Update-NHNotesText $text ('[\uE000-\uE07F]|' + $script:NHNotesUnsafe) ''
    # Backslash escapes stay literal: hide them from the span rules, restore at the end.
    $text = Update-NHNotesText $text '\\([\\`*_{}\[\]()#+\-.!>~|])' { param($m) [string][char](0xE000 + [int][char]$m.Groups[1].Value) }
    $text = Update-NHNotesText $text '^[ \t]*\[[^\]\n]+\]:[ \t]*\S.*$' ''
    $text = Update-NHNotesText $text '^[ \t]*(?:```|~~~).*$' ''
    $text = Update-NHNotesText $text '</?[A-Za-z][^<>\n]*>' ''
    $text = Update-NHNotesText $text '!\[([^\[\]\n]*)\]\([^()\n]*\)' '$1'
    $text = Update-NHNotesText $text '\[([^\[\]\n]*)\]\([^()\n]*\)' '$1'
    $text = Update-NHNotesText $text '\[([^\[\]\n]+)\]\[[^\[\]\n]*\]' '$1'
    $text = Update-NHNotesText $text '<(?:https?|ftp|mailto):[^<>\s]*>' '' 'Multiline,IgnoreCase'
    $text = Update-NHNotesText $text '\b(?:https?|ftp)://\S+|\bwww\.\S+' '' 'Multiline,IgnoreCase'
    $text = Update-NHNotesText $text '^[ \t]*(?:(?:-[ \t]*){3,}|(?:\*[ \t]*){3,}|(?:_[ \t]*){3,}|={3,})$' ''
    $text = Update-NHNotesText $text '^[ \t]*>[ \t]?' ''
    $text = Update-NHNotesText $text '^[ \t]{0,3}#{1,6}[ \t]+([^\n]*)$' { param($m) $m.Groups[1].Value.TrimEnd([char[]]" `t#") }
    $text = Update-NHNotesText $text '^([ \t]*)[-*+][ \t]+' { param($m) $m.Groups[1].Value + [char]0x2022 + ' ' }
    $text = Update-NHNotesText $text '`+([^`\n]*)`+' '$1'
    $text = Update-NHNotesText $text '\*\*(?=\S)([^*\n]+?)(?<=\S)\*\*|__(?=\S)([^_\n]+?)(?<=\S)__' '$1$2'
    $text = Update-NHNotesText $text '~~(?=\S)([^~\n]+?)(?<=\S)~~' '$1'
    $text = Update-NHNotesText $text '(?<![\w*])\*(?=\S)([^*\n]+?)(?<=\S)\*(?!\*)|(?<![\w_])_(?=\S)([^_\n]+?)(?<=\S)_(?![\w_])' '$1$2'
    $text = Update-NHNotesText $text '[\uE000-\uE07F]' { param($m) [string][char]([int][char]$m.Value - 0xE000) }
    $text = Update-NHNotesText ([Net.WebUtility]::HtmlDecode($text)) $script:NHNotesUnsafe ''
    $text = Update-NHNotesText $text '(?<=\S) {2,}' ' '
    $text = Update-NHNotesText $text '[ \u00A0]+$' ''
    $text = (Update-NHNotesText $text '\n{3,}' "`n`n").Trim("`n")
    if ($text.Trim().Length -eq 0) { return $null }
    return Limit-NHReleaseNotes $text
}

function Get-NHRetainedReleaseNotes($Releases, [string[]]$Versions) {
    # Plain notes of the published releases whose versions are wanted.
    $notes = @{}
    foreach ($release in @($Releases)) {
        $version = Get-UpdateReleaseVersion $release
        if ($null -eq $version) { continue }
        $name = $version.ToString()
        if ($Versions -cnotcontains $name -or $notes.ContainsKey($name)) { continue }
        $draft = $release.PSObject.Properties['draft']
        $body = $release.PSObject.Properties['body']
        if ($null -eq $draft -or $draft.Value -isnot [bool] -or $draft.Value -or $null -eq $body) { continue }
        $text = ConvertTo-NHPlainReleaseNotes $body.Value
        if ($text) { $notes[$name] = $text }
    }
    return $notes
}

function Write-NHCheckNotes([string]$MetadataPath, [string]$Channel, [string[]]$Versions, [string]$Output) {
    # Runs in the check child on the response it already downloaded. Notes are
    # optional: any failure leaves no file and the offer stands without them.
    try {
        Assert-UpdatePlainPath $Output
        if ($Channel -ceq 'beta') { $releases = Read-UpdateReleaseList $MetadataPath }
        else { $releases = @(Read-UpdateJSON $MetadataPath 1048576) }
        $notes = Get-NHRetainedReleaseNotes $releases @($Versions | Where-Object { $_ })
        [IO.File]::WriteAllText($Output, ($notes | ConvertTo-Json -Compress), (New-Object Text.UTF8Encoding($false)))
    } catch {
        if ([IO.File]::Exists($Output)) { [IO.File]::Delete($Output) }
    }
}

function Read-NHReleaseNotesFile([string]$Path) {
    $notes = @{}
    try {
        Assert-PlainPath $Path
        if (-not [IO.File]::Exists($Path) -or (New-Object IO.FileInfo($Path)).Length -gt 524288) { return $notes }
        $value = [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8) | ConvertFrom-Json
        if ($null -eq $value -or $value -is [string] -or $value -is [ValueType] -or $value -is [Array]) { return $notes }
        foreach ($property in $value.PSObject.Properties) {
            if ($property.Name -cmatch $script:NHNotesVersionPattern -and $property.Value -is [string] -and
                $property.Value.Length -le $script:NHNotesMaximumChars + 64) { $notes[$property.Name] = $property.Value }
        }
    } catch { } # Missing or malformed notes show the bundled text instead.
    return $notes
}

function Get-NHBundledReleaseNotes([string]$Runtime, [string]$Version) {
    # The installed version's section of the changelog shipped in its package.
    if (-not $Runtime -or -not $Version) { return $null }
    try {
        $path = Join-Path $Runtime $script:NHNotesBundledName
        Assert-PlainPath $path
        if (-not [IO.File]::Exists($path) -or (New-Object IO.FileInfo($path)).Length -gt 1048576) { return $null }
        $text = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8).Replace("`r`n", "`n")
    } catch { return $null }
    $heading = [regex]::Match($text, '^##[ \t]+v?' + [regex]::Escape($Version) + '(?![0-9.])[^\n]*$', 'Multiline')
    if (-not $heading.Success) { return $null }
    $start = $heading.Index + $heading.Length
    $next = [regex]::Match($text.Substring($start), '^##?[ \t]', 'Multiline')
    $length = $text.Length - $start
    if ($next.Success) { $length = $next.Index }
    return ConvertTo-NHPlainReleaseNotes $text.Substring($start, $length)
}

function Format-NHReleaseNotes([string]$Track, [string]$Installed, [string]$Offer, $Fetched, [string]$Offline) {
    # Newest first, each version clearly headed. Fetched is $null when no check succeeded.
    $entries = @{}
    if ($Installed) { $entries[$Installed] = 'installed' }
    if ($Offer -and $Offer -cne $Installed) { $entries[$Offer] = 'available update' }
    $sections = New-Object Collections.Generic.List[string]
    if ($Track -ceq 'beta') { $sections.Add('Beta track') } else { $sections.Add('Stable track') }
    # A beta package starts with an empty stable track.
    if (-not $Installed) { $sections.Add($(if ($Track -ceq 'beta') { 'No beta is installed yet.' } else { 'No stable version is installed yet.' })) }
    foreach ($version in @($entries.Keys | Sort-Object { [Version]$_ } -Descending)) {
        $text = $null
        if ($null -ne $Fetched -and $Fetched.ContainsKey($version)) { $text = $Fetched[$version] }
        if (-not $text -and $version -ceq $Installed) { $text = $Offline }
        if (-not $text -and $null -ne $Fetched) { $text = $script:NHNotesMissing }
        if (-not $text) { $text = $script:NHNotesOffline }
        $heading = 'Version ' + $version + ' (' + $entries[$version] + ')'
        $sections.Add($heading + "`n" + ('=' * $heading.Length) + "`n`n" + $text)
    }
    return ($sections -join "`n`n`n")
}

function Get-NHPlayerInstalledVersion($View, [string]$Track) {
    $runtime = $View.State.Runtimes[$Track]
    if ($null -eq $runtime) { return '' }
    return (Get-NHProductVersion $runtime).ToString()
}

function Get-NHPlayerNotesArguments($View, [string]$Path) {
    $arguments = @('-NotesOutput', $Path)
    $installed = Get-NHPlayerInstalledVersion $View $View.State.Track
    if ($installed) { $arguments += @('-NotesFor', $installed) }
    return $arguments
}

function Show-NHPlayerNotes($View, [string]$Original) {
    # Checks once per session and track; offline shows the bundled notes.
    if ($View.State.Busy) { return }
    $track = $View.State.Track
    if (-not $View.State.Notes.ContainsKey($track)) {
        $View.Status.Text = 'Loading release notes...'
        $launches = $View.State.Launches
        Invoke-NHPlayerUpdate $View $Original $false
        # Play stopped this check and ran the game; the notes are not wanted now.
        if ($View.State.Launches -ne $launches) { return }
        if (-not $View.State.Notes.ContainsKey($track)) {
            $View.Status.Text = 'Could not reach the release server. Showing the notes included with this version.'
        }
    }
    $installed = Get-NHPlayerInstalledVersion $View $track
    $offer = ''
    if ($null -ne $View.State.Offer) { $offer = [string]$View.State.Offer.version }
    $fetched = $null
    if ($View.State.Notes.ContainsKey($track)) { $fetched = $View.State.Notes[$track] }
    $offline = Get-NHBundledReleaseNotes $View.State.Runtimes[$track] $installed
    Show-NHReleaseNotesWindow $View.Form $track (Format-NHReleaseNotes $track $installed $offer $fetched $offline)
}

function Show-NHReleaseNotesWindow($Owner, [string]$Track, [string]$Text) {
    Initialize-NHWindowsLook
    $form = New-Object Windows.Forms.Form
    Start-NHFormLook $form 560 460
    $label = 'Stable'
    if ($Track -ceq 'beta') { $label = 'Beta' }
    $form.Text = "What's new - " + $label
    $form.MinimumSize = New-Object Drawing.Size(360,260)
    $form.StartPosition = 'CenterParent'
    $form.MinimizeBox = $false
    $form.ShowInTaskbar = $false
    $close = New-Object Windows.Forms.Button
    $close.Text = 'Close'
    $close.Location = New-Object Drawing.Point(434,412)
    $close.Size = New-Object Drawing.Size(110,32)
    $close.Anchor = 'Bottom,Right'
    $close.DialogResult = 'OK'
    Set-NHButtonLook $close
    $form.Controls.Add((New-NHReleaseNotesBox $Text))
    $form.Controls.Add($close)
    $form.AcceptButton = $close
    $form.CancelButton = $close
    $form.ActiveControl = $close
    Complete-NHFormLook $form
    try { $null = $form.ShowDialog($Owner) } finally { $form.Dispose() }
}

function New-NHReleaseNotesBox([string]$Text) {
    # A read-only TextBox shows text only: no markup, no clickable links, no browser.
    # Its frame gives the text the Linux window's 8-pixel margin.
    $frame = New-Object Windows.Forms.Panel
    $frame.Location = New-Object Drawing.Point(16,16)
    $frame.Size = New-Object Drawing.Size(528,384)
    $frame.Anchor = 'Top,Bottom,Left,Right'
    $frame.Padding = New-Object Windows.Forms.Padding(8)
    $frame.BackColor = Get-NHLookColor 'Field'
    $frame.BorderStyle = 'FixedSingle'
    $box = New-Object Windows.Forms.TextBox
    $box.Multiline = $true
    $box.ReadOnly = $true
    $box.WordWrap = $true
    $box.ScrollBars = 'Vertical'
    $box.MaxLength = 262144
    $box.BorderStyle = 'None'
    $box.BackColor = Get-NHLookColor 'Field'
    $box.ForeColor = Get-NHLookColor 'Text'
    $box.Dock = 'Fill'
    $box.Text = $Text.Replace("`n", "`r`n")
    # Dark scroll bars on Windows 10 1809+ and 11.
    $box.add_HandleCreated({ param($sender, $e) Set-NHControlTheme $sender 'DarkMode_Explorer' })
    $frame.Controls.Add($box)
    return $frame
}
