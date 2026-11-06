@echo off
rem Pull (rebase) then push to GitHub using gh credentials. Output to git_push.log.
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
"C:\msys64\usr\bin\bash.exe" -lc "cd ~/SynTHBloK && bash git_sync.sh 2>&1 | tee git_push.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 (
  echo.
  echo SYNC/PUSH FAILED - see git_push.log
) else (
  echo.
  echo PUSHED
)
pause
