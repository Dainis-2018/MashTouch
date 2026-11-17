# list tracks involved in clearance/short DRC errors (from a DRC report) as start/end keys
import pcbnew, sys, re
b = pcbnew.LoadBoard(sys.argv[1]); rep = open(sys.argv[2]).read()
locs = set()
for blk in re.split(r'\n(?=\[)', rep):
    if not re.match(r'\[(clearance|shorting_items|solder_mask_bridge)', blk): continue
    for m in re.finditer(r'@\(([\d.]+) mm, ([\d.]+) mm\): Track', blk): locs.add((float(m.group(1)), float(m.group(2))))
out = open(sys.argv[3], 'a')
for t in b.GetTracks():
    if t.GetClass() != 'PCB_TRACK': continue
    for (x, y) in locs:
        p = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
        if t.HitTest(p, 1000):
            out.write('%d,%d,%d,%d\n' % (t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y))
