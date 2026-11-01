@echo off
rem Builds and installs the SynTHBloK plugin with MSYS2, and saves the full output to build.log
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
"C:\msys64\usr\bin\bash.exe" -lc "cd ~/SynTHBloK && export RACK_DIR=~/Rack-SDK && make -j4 devinstall 2>&1 | tee build.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 (
  echo.
  echo BUILD FAILED - see build.log
) else (
  echo.
  echo BUILD OK - restart Rack to load it
)
pause
