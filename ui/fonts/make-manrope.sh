#!/bin/bash
# Rebuilds Manrope.ttf, the font of the wordmark only (Manrope 800, 13.5 px in the prototype):
# the variable font of Google Fonts pinned at wght 800, kept to the Latin and Vietnamese ranges.
# snapone's copy stops at wght 600. --update-name-table names the family "Manrope" (name ID 16).
# Needs: curl, python3 with fonttools (in Ubuntu-24.04: PATH=~/.venv-tools/bin:$PATH).
set -euo pipefail
cd "$(dirname "$0")"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
curl -fsSL -o "$tmp/full.ttf" "https://raw.githubusercontent.com/google/fonts/main/ofl/manrope/Manrope%5Bwght%5D.ttf"
python3 -m fontTools.varLib.instancer "$tmp/full.ttf" wght=800 --update-name-table -q -o "$tmp/static.ttf"
latin=U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+0304,U+0308,U+0329,U+2000-206F,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,U+FEFF,U+FFFD
vietnamese=U+0102-0103,U+0110-0111,U+0128-0129,U+0168-0169,U+01A0-01A1,U+01AF-01B0,U+0300-0301,U+0303-0304,U+0308-0309,U+0323,U+0329,U+1EA0-1EF9,U+20AB
python3 -m fontTools.subset "$tmp/static.ttf" --unicodes="$latin,$vietnamese" --name-IDs='*' --output-file=Manrope.ttf
ls -l Manrope.ttf
