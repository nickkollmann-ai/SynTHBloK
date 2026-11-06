#!/usr/bin/env bash
set -e
cd ~/SynTHBloK
# The clean_build.bat commit already exists locally; sync with remote then push.
git pull --rebase --autostash
git -c credential.helper= -c 'credential.helper=!"/c/Program Files/GitHub CLI/gh.exe" auth git-credential' push
echo "PUSH DONE"
