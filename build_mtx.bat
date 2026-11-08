@echo off
title SynTHBloK - build + install the MTX pack (RANDROYD)
rem Builds and installs only the MTX pack (repo root), where RANDROYD lives. Close Rack first. Output in build.log.
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
"C:\msys64\usr\bin\bash.exe" -lc "cd ~/SynTHBloK && export RACK_DIR=~/Rack-SDK SYNTHBLOK_DEV=1 && make -j4 devinstall 2>&1 | tee build.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 (echo. & echo BUILD FAILED - see build.log) else (echo. & echo BUILD OK - start Rack to load it)
pause
