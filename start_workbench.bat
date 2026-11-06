@echo off
rem One-click launcher for the SynTHBloK workbench (serves design_watch.html on
rem 127.0.0.1:8766 + Tailscale, starts the feeds, opens it in Firefox).
cd /d C:\msys64\home\nickk\SynTHBloK
powershell -ExecutionPolicy Bypass -File "tools\open_workbench.ps1"
pause
