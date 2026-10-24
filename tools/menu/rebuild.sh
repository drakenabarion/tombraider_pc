#!/usr/bin/env bash
# Rebuild scripts/menus/mainmenu-quit.bbmenu after changing edit_main3d.py.
#   tools/menu/rebuild.sh <game dir> <ffdec.jar>
# Needs Java and JPEXS FFDec (tested with 26.4.0). Work files go to out/menu-work.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
game=$1 ffdec=$2 work=out/menu-work
rm -rf -- "$work"; mkdir -p "$work/imp/scripts/__Packages"
python3 scripts/tiger.py gfx "$game" PRIORITY0 "$work/movies" >/dev/null
original=$work/movies/PRIORITY0_0a1d4a56_5.gfx
java -jar "$ffdec" -format script:pcode -selectclass State_Main3D -export script "$work/exp" "$original" >/dev/null
python3 tools/menu/edit_main3d.py "$work/exp/scripts/__Packages/State_Main3D.pcode" \
    "$work/imp/scripts/__Packages/State_Main3D.pcode"
java -jar "$ffdec" -importScript "$original" "$work/mainmenu.gfx" "$work/imp" >/dev/null
python3 scripts/menu_patch.py make "$original" "$work/mainmenu.gfx" scripts/menus/mainmenu-quit.bbmenu
