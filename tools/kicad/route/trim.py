# remove free-standing GND stitching vias (unlocked, no track ends on them); keep those in the buck area
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.FromMM
ends = set()
for t in b.GetTracks():
    if t.GetClass() == 'PCB_TRACK': ends.add((t.GetStart().x, t.GetStart().y)); ends.add((t.GetEnd().x, t.GetEnd().y))
vias = [t for t in b.GetTracks() if t.GetClass() == 'PCB_VIA' and t.GetNetname() == 'GND']
n = 0
for v in vias:
    p = v.GetPosition(); x, y = pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)
    if v.IsLocked() or (p.x, p.y) in ends: continue
    if 115 <= x <= 133 and 136 <= y <= 152: continue
    b.Delete(v); n += 1
print('GND vias', len(vias), 'removed', n)
b.Save(sys.argv[1])
