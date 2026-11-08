@echo off
rem Starts CLAUDE JOBS hidden: Claude runs the repo scripts without windows over your work. Safe to run twice.
start "" /min powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File C:\msys64\home\nickk\SynTHBloK\tools\claude_jobs.ps1
