@echo off
rem Converts Rack patches to SynTHBloK's new module slugs (tools\convert_slugs.py; backups are kept).
rem Double-click: converts the repo's patches, Rack's autosave (the session Rack reopens) and the .vcv files on the
rem Desktop. Or drop .vcv files / folders onto this file to convert those. Close Rack first.
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
cd /d "%~dp0"
if "%~1"=="" (
  python3 tools\convert_slugs.py patches "%LOCALAPPDATA%\Rack2\autosave" "%USERPROFILE%\Desktop"
) else (
  python3 tools\convert_slugs.py %*
)
if errorlevel 1 (
  echo.
  echo SOME PATCHES FAILED - see above
) else (
  echo.
  echo DONE - open Rack
)
pause
