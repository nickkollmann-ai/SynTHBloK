@echo off
rem Close Rack, install the MTX pack, open BACKDROP alone in an empty patch.
powershell -NoProfile -ExecutionPolicy Bypass -File C:\msys64\home\nickk\SynTHBloK\tools\install_and_open.ps1 -Patch patches\BACKDROP_alone_test.vcv
