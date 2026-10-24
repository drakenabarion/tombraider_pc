#!/usr/bin/env bash
# Rebuild scripts/menus/*.bbmenu after changing edit_main3d.py or edit_display.py.
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

# Options > Display: the class from edited ActionScript source, then trace() removed from every
# script (P-code) so the movie fits in the original's space.
display=$work/movies/PRIORITY0_c694569a_14.gfx
mkdir -p "$work/disp-as/scripts/__Packages"
java -jar "$ffdec" -selectclass State_OptionsDisplay -export script "$work/disp-exp" "$display" >/dev/null
python3 tools/menu/edit_display.py "$work/disp-exp/scripts/__Packages/State_OptionsDisplay.as" \
    "$work/disp-as/scripts/__Packages/State_OptionsDisplay.as"
java -jar "$ffdec" -importScript "$display" "$work/display1.gfx" "$work/disp-as" >/dev/null
java -jar "$ffdec" -format script:pcode -export script "$work/disp-pcode" "$work/display1.gfx" >/dev/null
python3 tools/menu/pcode.py strip "$work/disp-pcode" "$work/disp-imp"
java -jar "$ffdec" -importScript "$work/display1.gfx" "$work/display.gfx" "$work/disp-imp" >/dev/null
python3 scripts/menu_patch.py make "$display" "$work/display.gfx" scripts/menus/options-display.bbmenu
