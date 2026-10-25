#!/usr/bin/env bash
# B33: every Photoshop script of tools/ps-golden with what it reads, staged into one folder laid out as the scripts
# expect it, for Photoshop on Windows (README.md, section B33): B4's blend.jsx with base.png and top.png; p1/ (lib.jsx
# and B30, D32, F31's scripts) with p1/in/ and the LUTs of p1/lut/; strokes.jsx, select.jsx, resample.jsx, psd.jsx with
# strokes/in/, select/in/, resample-in/; fonts/ (Space Grotesk, to install). Results (out/, p1/out/) are left as they
# are: each script writes over its own folder, b33-collect.sh copies only what an index.json names.
# Usage: tools/ps-golden/b33-stage.sh [folder]   (default /mnt/c/Users/Teko/Downloads/ps-golden; C:\... works too)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dst=${1:-/mnt/c/Users/Teko/Downloads/ps-golden}
case $dst in [A-Za-z]:[\\/]*) dst=$(wslpath -u "$dst") ;; esac
shopt -s nullglob

# put <folder in dst> <file>...: the files copied in. mirror: the same, and the folder holds those files only (an
# input the repo no longer has goes).
put() {
  local to=$dst/$1
  shift
  [ $# -gt 0 ] || { echo "b33-stage: nothing to copy into $to (pb-bench writes the inputs)" >&2; exit 1; }
  mkdir -p "$to"
  cp -f -- "$@" "$to/"
}
mirror() {
  local rel=$1 f s keep
  shift
  [ $# -gt 0 ] || put "$rel"
  for f in "$dst/$rel"/*; do
    keep=
    for s in "$@"; do if [ "$(basename "$s")" = "$(basename "$f")" ]; then keep=1; fi; done
    if [ -f "$f" ] && [ -z "$keep" ]; then rm -f -- "$f"; fi
  done
  put "$rel" "$@"
}

g=$repo/tests/golden/ps
put . "$here"/blend.jsx "$g"/base.png "$g"/top.png "$here"/strokes.jsx "$here"/select.jsx "$here"/resample.jsx \
      "$here"/psd.jsx
put p1 "$here"/p1/*.jsx
mirror p1/in "$g"/p1/in/*.png
mirror p1/lut "$repo"/tests/data/lut/*
mirror strokes/in "$g"/strokes/in/*
mirror select/in "$g"/select/in/*.png
mirror resample-in "$g"/resample/in/*.png
mirror fonts "$repo"/ui/fonts/SpaceGrotesk.ttf "$repo"/ui/fonts/SpaceGrotesk-SemiBold.ttf \
       "$repo"/ui/fonts/OFL-SpaceGrotesk.txt

win=$(wslpath -w "$dst" 2>/dev/null || echo "$dst")
staged=$(find "$dst" \( -path "$dst/out" -o -path "$dst/p1/out" -o -path "$dst/ours" \) -prune -o -type f \
              -printf '%s\n' | awk '{ n++; s += $1 } END { printf "%d files, %.2f MB", n, s / 1048576 }')
echo "staged $staged: $dst ($win)"
for d in out p1/out; do
  if [ -n "$(find "$dst/$d" -type f -print -quit 2>/dev/null)" ]; then
    echo "  ${d/\//\\}\\ holds an earlier run's results: kept, each script writes over its own folder"
  fi
done
ours=("$dst"/ours/*.ps[db])
if [ ${#ours[@]} -gt 0 ]; then echo "  ours\\ holds ${#ours[@]} PSD files: psd.jsx will ask, No is pass one (B33)"; fi
cat <<EOF
Fonts: $win\\fonts\\SpaceGrotesk*.ttf: right click, Install for all users; then restart Photoshop.
Photoshop, File > Scripts > Browse..., one after the other, each to its "Done" (README.md, B33):
   1  $win\\blend.jsx          B4    about 1 min
   2  $win\\p1\\blend.jsx       B30   about 8 min
   3  $win\\p1\\adjust.jsx      D32   about 7 min
   4  $win\\p1\\gradient.jsx    D32   about 3 min
   5  $win\\p1\\filters.jsx     F31   about 6 min
   6  $win\\p1\\styles.jsx      F31   about 5 min
   7  $win\\strokes.jsx        C41   about 4 min
   8  $win\\select.jsx         S39   about 4 min
   9  $win\\resample.jsx       T38   about 12 min
  10  $win\\psd.jsx            E33   10-15 min
Then, in WSL: tools/ps-golden/b33-collect.sh${1:+ $(printf '%q' "$dst")}
EOF
