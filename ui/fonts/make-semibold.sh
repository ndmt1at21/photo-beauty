#!/bin/bash
# Makes SpaceGrotesk-SemiBold.ttf: SpaceGrotesk.ttf (the variable font, wght 300–700) pinned at wght 600, named as
# Google Fonts' static SemiBold (family "Space Grotesk" in name ID 16, "SemiBold" in 17), OS/2 weight 600. The variable
# font's named instances are 300, 400, 500 and 700 only, and Qt registers just those, so Font.DemiBold drew Medium; the
# prototype's 600 (.btnp, dialog titles, an adjustment's name) is Space Grotesk at wght 600 from Google Fonts. Same
# license as the variable font: OFL-SpaceGrotesk.txt (no Reserved Font Name). JetBrains Mono needs none: the prototype
# loads it at 400 and 500 only. The variable font's STAT has no 600, so the names are set here, not by the instancer.
# Needs: python3 with fonttools (in Ubuntu-24.04: PATH=~/.venv-tools/bin:$PATH).
set -euo pipefail
cd "$(dirname "$0")"
python3 -m fontTools.varLib.instancer SpaceGrotesk.ttf wght=600 -q -o SpaceGrotesk-SemiBold.ttf
python3 - <<'PY'
from fontTools.ttLib import TTFont
f = TTFont("SpaceGrotesk-SemiBold.ttf")
names = {1: "Space Grotesk SemiBold", 2: "Regular", 3: "SpaceGrotesk-SemiBold", 4: "Space Grotesk SemiBold",
         6: "SpaceGrotesk-SemiBold", 16: "Space Grotesk", 17: "SemiBold"}
t = f["name"]
t.names = [n for n in t.names if n.nameID not in names and n.nameID < 256]  # the axes' names went with fvar, STAT
for i, s in names.items():
    t.setName(s, i, 3, 1, 0x409)
f["OS/2"].usWeightClass = 600
f["OS/2"].fsSelection = (f["OS/2"].fsSelection & ~0b1100001) | 0b1000000  # REGULAR, not BOLD or ITALIC
f["head"].macStyle = 0
for tag in ("STAT",):
    if tag in f:
        del f[tag]
f.save("SpaceGrotesk-SemiBold.ttf")
PY
ls -l SpaceGrotesk-SemiBold.ttf
