@echo off
rem Regenerates every plugin.json, registration and Makefile (MTX pack + singles) from tools\lineup.json.
rem Output to make_plugins.log. Run before build_and_install.bat when the lineup changes.
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
cd /d "%~dp0"
python3 tools\make_plugins.py > make_plugins.log 2>&1
set RC=%errorlevel%
type make_plugins.log
if not "%RC%"=="0" (
  echo.
  echo MAKE_PLUGINS FAILED - see make_plugins.log
) else (
  echo.
  echo PLUGINS GENERATED - now run build_and_install.bat
)
pause
