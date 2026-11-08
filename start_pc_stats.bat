@echo off
rem Starts the PC telemetry feeder hidden (THINKING HARDWARE band + top bar PC pill). Safe to run twice.
start "" /min powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File C:\msys64\home\nickk\SynTHBloK\tools\pc_stats.ps1
