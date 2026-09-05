@echo off
rem Heroes III: New Horizons - Windows player entry point
rem SPDX-License-Identifier: GPL-2.0-or-later
setlocal DisableDelayedExpansion
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Start-New-Horizons.ps1"
set "NH_EXIT=%ERRORLEVEL%"
if not "%NH_EXIT%"=="0" (
    echo.
    echo New Horizons could not finish setup or play.
    echo Read the error above and README-New-Horizons.txt beside this file.
    echo No administrator access is required. Do not change machine execution policy.
    pause
)
exit /b %NH_EXIT%
