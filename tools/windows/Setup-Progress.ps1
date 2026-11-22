# SPDX-License-Identifier: GPL-2.0-or-later
# Optional presentation only; asset validation and activation stay in setup.
# Windows PowerShell 5.1. Keep this file ASCII.
# Every launcher and setup window shares one Windows 10/11 look whose palette,
# title and spacing follow the Linux launcher (tools/linux/PortableLauncherWindow.py).
if (-not (Get-Variable -Name NHLook -Scope Script -ErrorAction Ignore)) {
    $script:NHLook = @{Ready=$false; Native=$null; Icon=$null; Root=$PSScriptRoot}
}
$script:NHLookColors = @{Window='#211b16'; Text='#e9dfcc'; Title='#dfc27c'; Version='#c5b994'; Beta='#9fc6d6';
    Button='#42372a'; ButtonText='#f0e3c3'; Border='#8c7446'; Hover='#594832'; Pressed='#6e5a3c'; Play='#665126';
    DisabledText='#8a8273'; DisabledBorder='#504839'; Field='#2b231b'}

function Get-NHLookColor([string]$Name) { return [Drawing.ColorTranslator]::FromHtml($script:NHLookColors[$Name]) }

function Add-NHNativeMethod($Type, [string]$Library, [string]$Name, [Type]$Return, [Type[]]$Parameters) {
    $attributes = [Reflection.MethodAttributes]'Public,Static,HideBySig,PinvokeImpl'
    $method = $Type.DefinePInvokeMethod($Name, $Library, $attributes, 'Standard', $Return, $Parameters, 'Winapi', 'Unicode')
    $method.SetImplementationFlags('PreserveSig')
}

function Get-NHNativeLook {
    # Two Win32 entry points defined in memory with Reflection.Emit; Add-Type
    # would start the C# compiler on every launch.
    if ($null -ne $script:NHLook.Native) { return $script:NHLook.Native }
    $name = New-Object Reflection.AssemblyName 'NHNativeLook'
    $assembly = [Reflection.Emit.AssemblyBuilder]::DefineDynamicAssembly($name, 'Run')
    $type = $assembly.DefineDynamicModule('NHNativeLook').DefineType('NHNativeLook', 'Public,Abstract,Sealed')
    Add-NHNativeMethod $type 'user32.dll' 'SetProcessDPIAware' ([bool]) @()
    Add-NHNativeMethod $type 'dwmapi.dll' 'DwmSetWindowAttribute' ([int]) @([IntPtr], [int], [int].MakeByRefType(), [int])
    $script:NHLook.Native = $type.CreateType()
    return $script:NHLook.Native
}

function Initialize-NHWindowsLook {
    # Before this process creates its first window. powershell.exe declares no
    # DPI awareness, so Windows would stretch every window as a blurry bitmap;
    # the EXE's manifest cannot help because the windows belong to this process.
    # Without visual styles WinForms draws Windows 2000 controls.
    if ($script:NHLook.Ready) { return }
    $script:NHLook.Ready = $true
    try { $null = (Get-NHNativeLook)::SetProcessDPIAware() } catch { } # Optional: Windows then scales the window.
    Add-Type -AssemblyName System.Windows.Forms
    [Windows.Forms.Application]::EnableVisualStyles()
    # GDI text as in native dialogs; refused once any window exists.
    try { [Windows.Forms.Application]::SetCompatibleTextRenderingDefault($false) } catch { }
}

function New-NHLookFont([string]$Family, [single]$Size, [Drawing.FontStyle]$Style = 'Regular') {
    # Segoe UI is the Windows 10/11 interface font; a missing family falls back
    # to the system message font instead of Microsoft Sans Serif.
    $font = New-Object Drawing.Font($Family, $Size, $Style)
    if ($font.Name -ceq $Family) { return $font }
    $font.Dispose()
    return New-Object Drawing.Font([Drawing.SystemFonts]::MessageBoxFont.FontFamily, $Size, $Style)
}

function Get-NHLookIcon {
    # The game icon embedded in new-horizons.exe beside these scripts.
    if ($null -eq $script:NHLook.Icon) {
        $script:NHLook.Icon = $false
        try {
            $exe = Join-Path $script:NHLook.Root 'new-horizons.exe'
            if ([IO.File]::Exists($exe)) { $script:NHLook.Icon = [Drawing.Icon]::ExtractAssociatedIcon($exe) }
        } catch { }
    }
    if ($script:NHLook.Icon) { return $script:NHLook.Icon }
    return $null
}

function Set-NHDarkTitleBar($Form) {
    # Windows 10 20H1+ and 11 draw a dark caption; Windows 11 also takes the window colour.
    try {
        $native = Get-NHNativeLook
        $dark = 1
        if ($native::DwmSetWindowAttribute($Form.Handle, 20, [ref]$dark, 4) -ne 0) {
            $null = $native::DwmSetWindowAttribute($Form.Handle, 19, [ref]$dark, 4)
        }
        $caption = 0x00161B21 # COLORREF (0x00BBGGRR) of the Window colour.
        $null = $native::DwmSetWindowAttribute($Form.Handle, 35, [ref]$caption, 4)
    } catch { } # Older Windows keeps its standard caption.
}

function Start-NHFormLook($Form, [int]$Width, [int]$Height) {
    # Layout is written in 96-DPI pixels; Complete-NHFormLook scales it to the display.
    Initialize-NHWindowsLook
    $Form.SuspendLayout()
    $Form.AutoScaleDimensions = New-Object Drawing.SizeF(96, 96)
    $Form.AutoScaleMode = 'Dpi'
    $Form.Font = New-NHLookFont 'Segoe UI' 9
    $Form.BackColor = Get-NHLookColor 'Window'
    $Form.ForeColor = Get-NHLookColor 'Text'
    $Form.ClientSize = New-Object Drawing.Size($Width, $Height)
    $icon = Get-NHLookIcon
    if ($null -ne $icon) { $Form.Icon = $icon } else { $Form.ShowIcon = $false }
    $Form.add_HandleCreated({ param($sender, $e) Set-NHDarkTitleBar $sender })
}

function Complete-NHFormLook($Form) {
    $Form.ResumeLayout($false)
    $Form.PerformLayout()
}

function Write-NHDisabledButton($Button, $Paint) {
    # Flat WinForms buttons dim disabled text to almost the dark face colour.
    if ($Button.Enabled) { return }
    try {
        $bounds = $Button.ClientRectangle
        $Paint.Graphics.Clear($Button.BackColor)
        $pen = New-Object Drawing.Pen((Get-NHLookColor 'DisabledBorder'))
        try { $Paint.Graphics.DrawRectangle($pen, 0, 0, $bounds.Width - 1, $bounds.Height - 1) } finally { $pen.Dispose() }
        $flags = [Windows.Forms.TextFormatFlags]'HorizontalCenter,VerticalCenter,WordBreak'
        [Windows.Forms.TextRenderer]::DrawText($Paint.Graphics, $Button.Text, $Button.Font, $bounds,
            (Get-NHLookColor 'DisabledText'), $flags)
    } catch { } # Presentation only.
}

function Set-NHButtonLook($Button, [string]$Face = 'Button') {
    $Button.FlatStyle = 'Flat'
    $Button.UseVisualStyleBackColor = $false
    $Button.BackColor = Get-NHLookColor $Face
    $Button.ForeColor = Get-NHLookColor 'ButtonText'
    $Button.FlatAppearance.BorderColor = Get-NHLookColor 'Border'
    $Button.FlatAppearance.MouseOverBackColor = Get-NHLookColor 'Hover'
    $Button.FlatAppearance.MouseDownBackColor = Get-NHLookColor 'Pressed'
    $Button.add_Paint({ param($sender, $e) Write-NHDisabledButton $sender $e })
}

function Show-AssetImportProgress([int]$Files, [long]$Bytes, [switch]$StartingGame) {
    if ($env:NH_WINDOWS_BOOTSTRAP_UI -ne '1') { return $null }
    Initialize-NHWindowsLook
    $form = New-Object Windows.Forms.Form
    Start-NHFormLook $form 520 128
    $form.Text = if ($StartingGame) { 'New Horizons - starting game' } else { 'New Horizons - preparing original game assets' }
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.ControlBox = $false
    $form.add_FormClosing({ param($sender, $event) if (-not $sender.Tag) { $event.Cancel = $true } })
    $label = New-Object Windows.Forms.Label
    $label.Location = New-Object Drawing.Point(22, 20)
    $label.Size = New-Object Drawing.Size(476, 52)
    $label.Text = if ($StartingGame) { 'Preparing game artwork and starting New Horizons...' } else { 'Copying and verifying assets. Please keep this window open.' }
    $bar = New-Object Windows.Forms.ProgressBar
    $bar.Location = New-Object Drawing.Point(22, 86)
    $bar.Size = New-Object Drawing.Size(476, 16)
    $bar.Maximum = 1000
    if ($StartingGame) {
        $bar.Style = 'Marquee'
        $bar.MarqueeAnimationSpeed = 30
    }
    $form.Controls.Add($label)
    $form.Controls.Add($bar)
    Complete-NHFormLook $form
    $form.Show()
    $form.Refresh()
    [Windows.Forms.Application]::DoEvents()
    return [PSCustomObject]@{ Form = $form; Label = $label; Bar = $bar; Files = $Files; Bytes = $Bytes }
}

function Update-AssetImportProgress($Progress, [int]$Completed, [long]$Bytes, [string]$Phase) {
    if ($null -eq $Progress) { return }
    $Progress.Label.Text = ('{0}' + "`r`n" + '{1}/{2} files verified; {3:N1}/{4:N1} MiB') -f
        $Phase, $Completed, $Progress.Files, ($Bytes / 1MB), ($Progress.Bytes / 1MB)
    $fraction = if ($Progress.Bytes -gt 0) { $Bytes / [double]$Progress.Bytes } else { 0 }
    $Progress.Bar.Value = [Math]::Min(1000, [Math]::Max(0, [int](1000 * $fraction)))
    $Progress.Form.Refresh()
    [Windows.Forms.Application]::DoEvents()
}

function Close-AssetImportProgress($Progress) {
    if ($null -ne $Progress) {
        try {
            if (-not $Progress.Form.IsDisposed) {
                $Progress.Form.Tag = $true
                $Progress.Form.Close()
                $Progress.Form.Dispose()
            }
        } catch {
            # Optional presentation must not interrupt the owned game process.
        }
    }
}

function Show-GameStartupProgress($Progress, [AllowEmptyCollection()][string[]]$Arguments = @()) {
    # The ordinary double-click entry sets this flag. Command-line and report
    # invocations keep their terminal/automation contract, including headless.
    if ($env:NH_WINDOWS_BOOTSTRAP_UI -cne '1' -or $Arguments.Count -ne 0) {
        Close-AssetImportProgress $Progress
        return $null
    }
    try {
        if ($null -eq $Progress) { $Progress = Show-AssetImportProgress 0 0 -StartingGame }
        if ($null -eq $Progress) { return $null }
        $Progress.Form.Text = 'New Horizons - starting game'
        $Progress.Label.Text = 'Preparing game artwork and starting New Horizons...'
        $Progress.Bar.Style = 'Marquee'
        $Progress.Bar.MarqueeAnimationSpeed = 30
        Update-GameStartupProgress $Progress
        return $Progress
    } catch {
        Close-AssetImportProgress $Progress
        return $null
    }
}

function Update-GameStartupProgress($Progress) {
    if ($null -eq $Progress) { return }
    $Progress.Form.Refresh()
    [Windows.Forms.Application]::DoEvents()
}
