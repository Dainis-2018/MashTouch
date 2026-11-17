#!/bin/sh
# autoroute.sh in.kicad_pcb out.kicad_pcb tag
P="/c/Program Files/KiCad/10.0/bin/python.exe"; J="/c/Program Files/eparakstitajs3/runtime/bin/java.exe"
S="${WORK:?set WORK to a scratch folder holding fr/freerouting-2.5.0.jar}"
cp "$(dirname "$0")/../../../hardware/backpack/backpack.kicad_pro" "${1%.kicad_pcb}.kicad_pro"; cp "$(dirname "$0")/../../../hardware/backpack/backpack.kicad_pro" "${2%.kicad_pcb}.kicad_pro"
IN=$(cygpath -w "$1"); DSN=$(cygpath -w "$S/$3.dsn"); DSNN=$(cygpath -w "$S/$3_n.dsn"); SES=$(cygpath -w "$S/$3.ses")
"$P" -c "import pcbnew,sys; b=pcbnew.LoadBoard(sys.argv[1]); print('dsn', pcbnew.ExportSpecctraDSN(b, sys.argv[2]))" "$IN" "$DSN" || exit 1
"$P" "$(dirname "$0")/strip_gnd.py" "$DSN" "$DSNN" >/dev/null || exit 1
timeout 1500 "$J" -jar "$S/fr/freerouting-2.5.0.jar" -de "$DSNN" -do "$SES" -mp ${MP:-100} --gui.enabled=false > "$S/$3.log" 2>&1
grep -v "not found" "$S/$3.log" | grep -E "Auto-routing stage completed|ptimiz.*(completed|finished)" | tail -2 | cut -c1-260
cp "$(dirname "$0")/../../../hardware/backpack/backpack.kicad_pro" "${2%.kicad_pcb}.kicad_pro"
"$P" "$(dirname "$0")/imp.py" "$1" "$SES" "$2"
"/c/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb drc --severity-all -o "$S/drc_$3.txt" "$2" >/dev/null
grep -E "^\[" "$S/drc_$3.txt" | sed 's/ at .*//;s/(.*//' | sort | uniq -c | sort -rn
grep -A3 "^\[unconnected" "$S/drc_$3.txt" | grep "@" | sed 's/.*\[//;s/\].*//' | sort | uniq -c | sort -rn | grep -v " GND$"
