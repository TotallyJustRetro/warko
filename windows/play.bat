@echo off
rem Drag a Game Boy ROM (.gb / .gbc) onto this file, or run:  play.bat "path\to\rom.gbc"
rem First run per ROM recompiles it (about a minute); later runs start instantly.
setlocal
set "MSYS=C:\msys64"
if not exist "%MSYS%\usr\bin\bash.exe" (
  echo MSYS2 not found at %MSYS%.
  echo Install it from https://www.msys2.org then run this again.
  pause & exit /b 1
)
if "%~1"=="" (
  echo Drag a ROM file onto play.bat.
  pause & exit /b 1
)
set "GBROM=%~f1"
set "GBHERE=%~dp0.."
set "GBHERE=%GBHERE:\=/%"
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
"%MSYS%\usr\bin\bash.exe" -lc "sh \"$GBHERE/windows/winrun.sh\""
if errorlevel 1 pause
