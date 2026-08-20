@echo off
REM SYJ EdgeMind — Windows build entry point.
REM
REM This is a THIN delegator, not a second independent build
REM implementation — every actual build step (tool checks, CMake
REM configure, CPU-mode selection, build) lives in build-windows.ps1.
REM Any flag this script doesn't recognize is passed straight through.
REM
REM Usage examples:
REM   build-windows.bat
REM   build-windows.bat -EnableAVX
REM   build-windows.bat -EnableAVX2 -Clean
REM   build-windows.bat -BuildDir mybuild -Config Debug
REM   build-windows.bat -Generator "Visual Studio 17 2022"

setlocal

where powershell >nul 2>nul
if errorlevel 1 (
    echo ERROR: powershell was not found on PATH.
    echo SYJ EdgeMind's Windows build requires PowerShell ^(included by
    echo default on Windows 10/11^) to run build-windows.ps1.
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" %*
exit /b %errorlevel%
