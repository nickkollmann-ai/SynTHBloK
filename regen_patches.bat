@echo off
title SynTHBloK - regenerate the 100 SB_ patches (RANDROYD engine)
cd /d C:\msys64\home\nickk\SynTHBloK
echo Regenerating SB_001..SB_100 with RANDROYD's song engine...
echo.
C:\msys64\usr\bin\env.exe MSYSTEM=MINGW64 CHERE_INVOKING=1 /usr/bin/bash -lc "python3 tools/make_series_patches.py 100 2>&1 | tee tools/regen_log.txt; echo EXIT=${PIPESTATUS[0]} >> tools/regen_log.txt"
echo.
echo Done. Log: tools\regen_log.txt
pause
