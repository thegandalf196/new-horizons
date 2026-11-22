"""What's new release notes in the Windows launcher: source contracts and offline PowerShell fixtures."""
# SPDX-License-Identifier: GPL-2.0-or-later
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = ROOT / 'tools/windows'
PS = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')
sys.path.insert(0, str(ROOT / 'tools/linux'))

import PortableReleaseNotes as linux_notes

# Shared with the Linux flattening so both launchers show the same text.
PARITY = ('## Highlights\n\n- **Bold** and *italic* and __strong__ and `code`\n  * nested ~~old~~ item\n> quoted\n'
          '---\n1. numbered\n<details>shown</details>\n\\*literal stars\\* &amp; more\n\n\n\nend',
          'See [the wiki](https://example.invalid/wiki), ![map](https://example.invalid/i.png), '
          '<https://example.invalid/auto>, https://example.invalid/bare and [ref][1].\n[1]: https://example.invalid/r\n'
          'Built by [Actions run #7](https://example.invalid/run).',
          'a\x1b[31mb\u202ec\u200bd&#27;e',
          '### Title ###\n\n\n\n_under_ and snake_case_name',
          '\n'.join('line %05d' % index for index in range(5000)))


class WindowsNotesSourceTest(unittest.TestCase):
    def test_notes_window_is_a_read_only_text_box_without_links_or_browser(self):
        source = (WINDOWS / 'Player-Launcher-Notes.ps1').read_text(encoding='ascii')
        window = source[source.index('function Show-NHReleaseNotesWindow'):]
        for required in ('New-Object Windows.Forms.TextBox', '$box.ReadOnly = $true', '$box.Multiline = $true',
                         "$box.ScrollBars = 'Vertical'", '$form.CancelButton = $close', '$form.ShowDialog($Owner)'):
            self.assertIn(required, window)
        for forbidden in ('WebBrowser', 'RichTextBox', 'LinkLabel', 'Process]::Start', 'Start-Process',
                          'DetectUrls', 'Invoke-WebRequest', 'Invoke-UpdateDownload', 'http://', 'https://'):
            self.assertNotIn(forbidden, source)

    def test_launcher_loads_notes_and_disables_the_button_while_busy(self):
        launcher = (WINDOWS / 'Player-Launcher.ps1').read_text()
        self.assertIn(". (Join-Path $PSScriptRoot 'Player-Launcher-Notes.ps1')", launcher)
        self.assertIn('@($View.Check,$View.Assets,$View.Track,$View.Notes)', launcher)
        self.assertIn('$view.Notes.add_Click({ Show-NHPlayerNotes $view $Original })', launcher)
        operation = (WINDOWS / 'Player-Launcher-Operation.ps1').read_text()
        self.assertIn(". (Join-Path $PSScriptRoot 'Player-Launcher-Notes.ps1')", operation)
        check = operation[operation.index("if ($Operation -eq 'Check')"):operation.index('} else {')]
        # Notes reuse the metadata file of this check; no second download.
        self.assertIn('Write-NHCheckNotes $metadata $Channel $wanted $NotesOutput', check)
        self.assertEqual(check.count('Get-NHLatestPlayerOffer'), 1)

    def test_original_launcher_hand_over_copies_notes_before_the_launcher(self):
        source = (WINDOWS / 'Update-NHOriginalLauncher.ps1').read_text()
        files = source[source.index('$script:NHOriginalLauncherFiles'):source.index('function ')]
        self.assertLess(files.index("'Player-Launcher-Notes.ps1'"), files.index("'Player-Launcher.ps1'"))


@unittest.skipUnless(PS, 'Set NH_TEST_POWERSHELL for offline launcher checks')
class WindowsNotesPowerShellTest(unittest.TestCase):
    def execute(self, body, **environment):
        with tempfile.TemporaryDirectory() as temporary:
            harness = Path(temporary) / 'harness.ps1'
            harness.write_text(r'''
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$runtime=Join-Path $env:FIXTURE 'runtime'
$null=[IO.Directory]::CreateDirectory((Join-Path $runtime 'config'))
[IO.File]::WriteAllText((Join-Path $runtime 'config/newHorizonsVersion.json'),'{"version":"0.92.3"}')
. (Join-Path $env:SOURCE 'tools/windows/Launch-Process.ps1')
. (Join-Path $env:SOURCE 'tools/windows/Player-Launcher.ps1')
. (Join-Path $env:SOURCE 'tools/windows/Player-Launcher-Operation.ps1')
function Require([bool]$Condition,[string]$Message) { if(-not $Condition){throw $Message} }
function New-FixtureRelease([string]$Version,[bool]$Prerelease,[string]$Body) {
    $name='New-Horizons-Windows-x64-0123456789ab.zip'
    return @{draft=$false;prerelease=$Prerelease;tag_name='v'+$Version;published_at='2026-11-21T12:00:00Z';body=$Body;
        html_url=$script:ReleaseRoot+'tag/v'+$Version;assets=@(@{name=$name;state='uploaded';size=100;
        digest='sha256:'+('a'*64);browser_download_url=$script:ReleaseRoot+'download/v'+$Version+'/'+$name})}
}
''' + body, encoding='ascii')
            result = subprocess.run([PS, '-NoProfile', '-NonInteractive', '-File', str(harness)],
                                    env=dict(os.environ, SOURCE=str(ROOT), FIXTURE=temporary, **environment),
                                    text=True, capture_output=True, timeout=60, check=False)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            return result.stdout

    def test_flattening_matches_the_linux_launcher(self):
        for index, body in enumerate(PARITY):
            with self.subTest(index=index):
                output = self.execute(r'''
$text=ConvertTo-NHPlainReleaseNotes ([IO.File]::ReadAllText($env:BODY,[Text.Encoding]::UTF8))
[IO.File]::WriteAllText($env:RESULT,(ConvertTo-Json -InputObject @{text=$text} -Compress),(New-Object Text.UTF8Encoding($false)))
''', BODY=self.write_temp(body), RESULT=str(self.result_path()))
                self.assertEqual(output, '')
                self.assertEqual(json.loads(self.result_path().read_text(encoding='utf-8'))['text'], linux_notes.flatten(body))

    def write_temp(self, text):
        handle = tempfile.NamedTemporaryFile('w', encoding='utf-8', suffix='.md', delete=False)
        self.addCleanup(os.unlink, handle.name)
        with handle:
            handle.write(text)
        return handle.name

    def result_path(self):
        if not hasattr(self, '_result'):
            handle = tempfile.NamedTemporaryFile(suffix='.json', delete=False)
            handle.close()
            self.addCleanup(os.unlink, handle.name)
            self._result = Path(handle.name)
        return self._result

    def test_malformed_bodies_and_compose_ordering(self):
        self.execute(r'''
foreach ($body in @($null, 7, '', '   ', '<!-- only -->')) { Require ($null -eq (ConvertTo-NHPlainReleaseNotes $body)) "Body $body" }
$long=ConvertTo-NHPlainReleaseNotes ('x' * 50000)
Require ($long.Length -eq 20000 + $script:NHNotesShortened.Length -and $long.EndsWith($script:NHNotesShortened)) 'No cap'
$text=Format-NHReleaseNotes 'stable' '0.92.7' '0.93.0' @{'0.93.0'='New things'} 'Installed things'
Require ($text.StartsWith('Stable track')) 'No track'
Require ($text.IndexOf('Version 0.93.0 (available update)') -lt $text.IndexOf('Version 0.92.7 (installed)')) 'Order'
Require ($text.Contains('New things') -and $text.Contains('Installed things')) 'Notes missing'
Require ((Format-NHReleaseNotes 'stable' '0.92.7' '' $null '').Contains($script:NHNotesOffline)) 'Offline'
Require ((Format-NHReleaseNotes 'beta' '' '' $null '').Contains('No beta is installed yet.')) 'Empty beta'
''')

    def test_check_operation_writes_notes_from_its_one_download(self):
        self.execute(r'''
$script:Downloads=0
function Invoke-UpdateDownload($URL,$Output,$MaximumBytes){$script:Downloads++;[IO.File]::WriteAllText($Output,$script:ReleaseText)}
$list=@((New-FixtureRelease '0.93.1' $true '- **Offer**'),(New-FixtureRelease '0.93.0' $true 'Installed'),
    (New-FixtureRelease '0.92.9' $false 'Other'))
$script:ReleaseText=ConvertTo-Json -InputObject $list -Depth 6
$Operation='Check'; $Original=$runtime; $Channel='beta'
$Output=Join-Path $env:FIXTURE 'result.json'; $NotesOutput=Join-Path $env:FIXTURE 'notes.json'; $NotesFor='0.93.0'
Require ((Invoke-NHPlayerOperation) -eq 0) 'Check failed'
Require ($script:Downloads -eq 1) 'Notes used another request'
$notes=Read-NHReleaseNotesFile $NotesOutput
Require ($notes.Count -eq 2 -and $notes['0.93.1'] -ceq ([string][char]0x2022 + ' Offer') -and $notes['0.93.0'] -ceq 'Installed') 'Wrong notes'
''')

    def test_offline_click_shows_bundled_notes(self):
        self.execute(r'''
[IO.File]::WriteAllText((Join-Path $runtime 'CHANGELOG-New-Horizons.md'),"## 0.92.3 Early Access`n`n- Bundled item`n`n## 0.92.2`n`n- Old`n")
$form=[PSCustomObject]@{}
$view=[PSCustomObject]@{Form=$form;Version=[PSCustomObject]@{Text=''};Status=[PSCustomObject]@{Text=''};
    Bar=[PSCustomObject]@{Visible=$false};Play=[PSCustomObject]@{Enabled=$true};Check=[PSCustomObject]@{Enabled=$true};
    Assets=[PSCustomObject]@{Enabled=$true};Install=[PSCustomObject]@{Enabled=$false};Notes=[PSCustomObject]@{Enabled=$true};
    Track=[PSCustomObject]@{Enabled=$true;SelectedIndex=0};
    State=[PSCustomObject]@{Runtime=$runtime;Offer=$null;Busy=$false;ExitCode=0;Track='stable';
        Runtimes=@{stable=$runtime;beta=$null};Code=(Join-Path $env:SOURCE 'tools/windows');Notes=@{};
        Checking=$false;PlayRequested=$false;CloseRequested=$false;Launches=0}}
$script:Checks=0
function Invoke-NHPlayerUpdate($View,$Original,$Install) { $script:Checks++ }
function Show-NHReleaseNotesWindow($Owner,$Track,$Text) { $script:Shown=$Text }
Show-NHPlayerNotes $view $runtime
Require ($script:Checks -eq 1) 'No check'
Require ($script:Shown.Contains([string][char]0x2022 + ' Bundled item') -and -not $script:Shown.Contains('Old')) 'Bundled notes missing'
Require ($view.Status.Text.Contains('Could not reach')) 'Offline status missing'
$view.State.Notes['stable']=@{'0.92.3'='Published'}
Show-NHPlayerNotes $view $runtime
Require ($script:Checks -eq 1 -and $script:Shown.Contains('Published')) 'Session notes not reused'
$view.State.Busy=$true
$script:Shown=''
Show-NHPlayerNotes $view $runtime
Require ($script:Shown -ceq '') 'Busy launcher showed notes'
''')


if __name__ == '__main__':
    unittest.main()
