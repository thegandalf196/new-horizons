"""The Windows launcher and setup windows share one modern, DPI-aware look (no native UI here)."""
# SPDX-License-Identifier: GPL-2.0-or-later
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = ROOT / 'tools/windows'
PS = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')
# Every script that shows a WinForms window, dialog or message box.
WINDOW_SCRIPTS = ('Setup-Progress.ps1', 'Player-Launcher.ps1', 'Player-Launcher-Notes.ps1', 'Start-New-Horizons.ps1')
WINDOW_MARKERS = ('New-Object Windows.Forms.Form', 'FolderBrowserDialog', 'Windows.Forms.MessageBox]::Show')


def functions(source):
    """Top-level PowerShell function bodies by name (these files keep `function` at column 0)."""
    starts = [(match.group(1), match.start()) for match in re.finditer(r'^function ([\w-]+)', source, re.M)]
    ends = [start for _, start in starts[1:]] + [len(source)]
    return {name: source[start:end] for (name, start), end in zip(starts, ends)}


class WindowsLauncherLookSourceTest(unittest.TestCase):
    def setUp(self):
        self.progress = (WINDOWS / 'Setup-Progress.ps1').read_text(encoding='ascii')
        self.look = functions(self.progress)

    def test_dpi_awareness_then_visual_styles_before_any_window(self):
        body = self.look['Initialize-NHWindowsLook']
        order = ['::SetProcessDPIAware()', 'Add-Type -AssemblyName System.Windows.Forms',
                 '[Windows.Forms.Application]::EnableVisualStyles()',
                 '[Windows.Forms.Application]::SetCompatibleTextRenderingDefault($false)']
        positions = [body.index(step) for step in order]
        self.assertEqual(positions, sorted(positions))
        self.assertIn('if ($script:NHLook.Ready) { return }', body)

    def test_every_window_entry_initializes_the_look_first(self):
        for name in WINDOW_SCRIPTS:
            source = (WINDOWS / name).read_text()
            # Only the shared initializer loads WinForms; a window can never precede it.
            self.assertEqual(source.count('Add-Type -AssemblyName System.Windows.Forms'),
                             1 if name == 'Setup-Progress.ps1' else 0, name)
            blocks = list(functions(source).values())
            if '\ntry {' in source:
                blocks.append(source[source.rindex('\ntry {'):])
            for block in blocks:
                for marker in WINDOW_MARKERS:
                    if marker in block:
                        with self.subTest(script=name, marker=marker):
                            self.assertIn('Initialize-NHWindowsLook', block[:block.index(marker)])

    def test_windows_scale_from_96_dpi_with_segoe_ui(self):
        start = self.look['Start-NHFormLook']
        for required in ('Initialize-NHWindowsLook', '$Form.SuspendLayout()',
                         '$Form.AutoScaleDimensions = New-Object Drawing.SizeF(96, 96)', "$Form.AutoScaleMode = 'Dpi'",
                         "$Form.Font = New-NHLookFont 'Segoe UI' 9", 'Set-NHDarkTitleBar'):
            self.assertIn(required, start)
        self.assertLess(start.index('SuspendLayout'), start.index('AutoScaleDimensions'))
        self.assertIn('$Form.ResumeLayout($false)', self.look['Complete-NHFormLook'])
        self.assertIn('[Drawing.SystemFonts]::MessageBoxFont', self.look['New-NHLookFont'])
        windows = {'Setup-Progress.ps1': 'Show-AssetImportProgress', 'Player-Launcher.ps1': 'New-NHPlayerView',
                   'Player-Launcher-Notes.ps1': 'Show-NHReleaseNotesWindow'}
        for name, function in windows.items():
            with self.subTest(window=function):
                body = functions((WINDOWS / name).read_text())[function]
                self.assertLess(body.index('Start-NHFormLook $form'), body.index('Complete-NHFormLook $form'))
                self.assertNotIn('ClientSize', body)

    def test_no_compiler_on_the_window_path(self):
        # Add-Type C# would start csc.exe on every launch; Reflection.Emit defines the two calls.
        for name in WINDOW_SCRIPTS:
            source = (WINDOWS / name).read_text()
            self.assertNotRegex(source, r'Add-Type\s+-(TypeDefinition|MemberDefinition)', name)
        self.assertIn('DefinePInvokeMethod', self.look['Add-NHNativeMethod'])

    def test_launcher_mirrors_the_linux_window(self):
        linux = (ROOT / 'tools/linux/PortableLauncherWindow.py').read_text()
        palette = dict(re.findall(r"(\w+)='(#[0-9a-f]{6})'", self.progress))
        for name, css in (('Window', 'window { background: '), ('Title', '.title { color: '),
                          ('Version', '.version { color: '), ('Beta', '.beta { color: '),
                          ('Play', '.play { background: ')):
            with self.subTest(color=name):
                self.assertIn(css + palette[name], linux)
        launcher = (WINDOWS / 'Player-Launcher.ps1').read_text()
        self.assertIn("$title.Text = 'NEW HORIZONS'", launcher)
        for label in ("'Play'", "'Check for updates'", "'Install update'", "'Original game files'", '"What\'s new"'):
            self.assertIn(label, launcher)
            self.assertIn(label.strip('\'"'), linux)

    def test_track_list_and_notes_scroll_bars_use_the_dark_theme(self):
        theme = self.look['Set-NHControlTheme']
        # A null class list keeps the control's class; an empty string would turn visual styles off.
        self.assertIn('::SetWindowTheme($Control.Handle, $Theme, [NullString]::Value)', theme)
        self.assertIn('} catch { }', theme)
        combo = self.look['Set-NHComboLook']
        for required in ("$Combo.DropDownStyle = 'DropDownList'", "$Combo.FlatStyle = 'Standard'",
                         "$Combo.DrawMode = 'OwnerDrawFixed'", 'Write-NHComboItem $sender $e',
                         "add_HandleCreated({ param($sender, $e) Set-NHControlTheme $sender 'DarkMode_CFD' })"):
            self.assertIn(required, combo)
        item = self.look['Write-NHComboItem']
        for face in ("'Field'", "'Button'", "'Hover'", "'ButtonText'", "'DisabledText'", 'ComboBoxEdit', 'NoPrefix'):
            self.assertIn(face, item)
        self.assertNotIn('SystemColors', item)
        launcher = functions((WINDOWS / 'Player-Launcher.ps1').read_text())['New-NHPlayerTrack']
        self.assertIn('Set-NHComboLook $list', launcher)
        self.assertNotIn('FlatStyle', launcher)
        notes = functions((WINDOWS / 'Player-Launcher-Notes.ps1').read_text())['New-NHReleaseNotesBox']
        self.assertIn("add_HandleCreated({ param($sender, $e) Set-NHControlTheme $sender 'DarkMode_Explorer' })", notes)


@unittest.skipUnless(PS, 'Set NH_TEST_POWERSHELL for offline launcher checks')
class WindowsLauncherLookPowerShellTest(unittest.TestCase):
    def test_native_calls_are_defined_in_memory_once_and_survive_reloading(self):
        with tempfile.TemporaryDirectory() as temporary:
            harness = Path(temporary) / 'look.ps1'
            harness.write_text(r'''
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $env:SOURCE 'tools/windows/Setup-Progress.ps1')
$first=Get-NHNativeLook
$script:NHLook.Ready=$true
. (Join-Path $env:SOURCE 'tools/windows/Setup-Progress.ps1')
$methods=@($first.GetMethods() | Where-Object { $_.DeclaringType -eq $first } | Sort-Object Name | ForEach-Object {
    $_.Name + ':' + $_.Attributes.HasFlag([Reflection.MethodAttributes]::PinvokeImpl) + ':' +
    $_.MethodImplementationFlags.HasFlag([Reflection.MethodImplAttributes]::PreserveSig) })
[PSCustomObject]@{methods=$methods;same=[Object]::ReferenceEquals($first,(Get-NHNativeLook));
    ready=$script:NHLook.Ready;dynamic=$first.Assembly.IsDynamic}|ConvertTo-Json -Compress
''', encoding='ascii')
            result = subprocess.run([PS, '-NoProfile', '-NonInteractive', '-File', str(harness)],
                                    env=dict(os.environ, SOURCE=str(ROOT)), text=True, capture_output=True,
                                    timeout=30, check=False)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(json.loads(result.stdout), {
            'methods': ['DwmSetWindowAttribute:True:True', 'SetProcessDPIAware:True:True', 'SetWindowTheme:True:True'],
            'same': True, 'ready': True, 'dynamic': True})


if __name__ == '__main__':
    unittest.main()
