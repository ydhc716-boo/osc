@echo off
REM ── SCO Firmware Build & Flash Environment ─────────────────────────────
REM Sets up PATH for ARM GCC, GNU Make, ST-Link, and OpenOCD.
REM Usage: Run this first, or paste each section into PowerShell.

set "ARM_GCC=C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin"
set "GNU_MAKE=C:\Program Files (x86)\GnuWin32\bin"
set "STLINK=%LOCALAPPDATA%\stlink\stlink-1.8.0-win32\bin"
set "OPENOCD=%LOCALAPPDATA%\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7\bin"

set "PATH=%ARM_GCC%;%GNU_MAKE%;%STLINK%;%OPENOCD%;%PATH%"

echo === SCO Firmware Tools ===
echo ARM GCC : ready
echo Make    : ready
echo ST-Link : ready
echo OpenOCD : ready
echo ==========================

cd /d C:\Users\17937\Desktop\SCO\firmware

echo.
echo Commands:
echo   make               - Build firmware
echo   make flash         - Flash via ST-Link (st-flash)
echo   make flash-ocd     - Flash via OpenOCD
echo   make clean         - Clean build artifacts
echo.

cmd /k
