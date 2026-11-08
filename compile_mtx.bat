@echo off
rem Compiles the MTX pack (RANDROYD) without installing it, so it can run while Rack is open. Output in build.log.
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
"C:\msys64\usr\bin\bash.exe" -lc "cd ~/SynTHBloK && export RACK_DIR=~/Rack-SDK SYNTHBLOK_DEV=1 && make -j4 2>&1 | tee build.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 (echo COMPILE FAILED - see build.log) else (echo COMPILE OK - install with build_mtx.bat once Rack is closed)
