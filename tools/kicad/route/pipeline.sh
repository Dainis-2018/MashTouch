#!/bin/sh
# full pipeline on the real board -> $S/pre_$1.kicad_pcb, routed -> $S/$1.kicad_pcb
S="${WORK:?set WORK to a scratch folder holding fr/freerouting-2.5.0.jar}"
P="/c/Program Files/KiCad/10.0/bin/python.exe"; B=/c/Users/daini/code/MashTouch/hardware/backpack/backpack.kicad_pcb
"$P" "$(dirname "$0")/place.py" $B $B | grep -v "not placed: \[\]"
"$P" "$(dirname "$0")/hand.py" $B && "$P" "$(dirname "$0")/fanout.py" $B >/dev/null && "$P" "$(dirname "$0")/fanfix.py" $B || exit 1
cp $B "$S/pre_$1.kicad_pcb"; cp "${B%.kicad_pcb}.kicad_pro" "$S/backpack.kicad_pro"
"$(dirname "$0")/autoroute.sh" "$S/pre_$1.kicad_pcb" "$S/$1.kicad_pcb" $1
