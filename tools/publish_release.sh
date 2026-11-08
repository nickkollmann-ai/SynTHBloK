#!/usr/bin/env bash
# Publish the GitHub Release for a tag whose build ran but whose "GitHub Release" step failed:
# downloads the packages that run built and creates the release with them.
#   bash tools/publish_release.sh 2.1.0 <run id>
set -e
cd ~/SynTHBloK
V="$1"; RUN="$2"
GH="/c/Program Files/GitHub CLI/gh.exe"
REPO=nickkollmann-ai/SynTHBloK
rm -rf release_pkgs
"$GH" run download "$RUN" -R "$REPO" -D release_pkgs
mapfile -t files < <(find release_pkgs -name '*.vcvplugin' | sort)
echo "${#files[@]} packages"
[ "${#files[@]}" -gt 0 ] || { echo "no packages downloaded"; exit 1; }
"$GH" release create "v$V" "${files[@]}" -R "$REPO" --verify-tag --title "SynTHBloK v$V" --generate-notes
rm -rf release_pkgs
echo "PUBLISHED https://github.com/$REPO/releases/tag/v$V"
