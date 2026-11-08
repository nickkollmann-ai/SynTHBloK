@echo off
title SynTHBloK - rebuild the old GEN_ patches (RANDROYD engine)
cd /d C:\msys64\home\nickk\SynTHBloK\tools
set PATH=C:\msys64\usr\bin;C:\msys64\mingw64\bin;%PATH%
C:\msys64\mingw64\bin\python3.exe make_series_patches.py --regen-gen > regen_gen_log.txt 2>&1
echo EXIT=%ERRORLEVEL%>> regen_gen_log.txt
type regen_gen_log.txt
