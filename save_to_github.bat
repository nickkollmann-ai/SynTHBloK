@echo off
rem Commit + push all work since the last push. Output to save_to_github.log.
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
"C:\msys64\usr\bin\bash.exe" -lc "cd ~/SynTHBloK && bash save_to_github.sh 2>&1 | tee save_to_github.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 (
  echo.
  echo SAVE FAILED - see save_to_github.log
) else (
  echo.
  echo SAVED TO GITHUB
)
pause
