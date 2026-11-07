@echo off
rem Clean rebuild: wipes build output, then builds and installs every SynTHBloK plugin with MSYS2: the MTX pack (repo root) and each single in singles\.
rem Development build (SYNTHBLOK_DEV=1: includes the PATCH GEN pictures menu). Full output in build.log.
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
"C:\msys64\usr\bin\bash.exe" -lc "cd ~/SynTHBloK && export RACK_DIR=~/Rack-SDK SYNTHBLOK_DEV=1 && { make clean && make -j4 devinstall && for d in singles/*/; do echo == $d; make -C $d clean && make -C $d -j4 devinstall || exit 1; done; } 2>&1 | tee build.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 (
  echo.
  echo BUILD FAILED - see build.log
) else (
  echo.
  echo BUILD OK - restart Rack to load it
)
pause
