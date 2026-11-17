# widen.py board target_mm [revert_list]: set tracks thinner than target to target, except positions listed in revert file
import pcbnew, sys, os
b = pcbnew.LoadBoard(sys.argv[1]); tgt = pcbnew.FromMM(float(sys.argv[2]))
skip = set()
if len(sys.argv) > 3 and os.path.exists(sys.argv[3]):
    for l in open(sys.argv[3]): skip.add(l.strip())
n = 0
for t in b.GetTracks():
    if t.GetClass() == 'PCB_TRACK' and t.GetWidth() < tgt:
        key = '%d,%d,%d,%d' % (t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y)
        if key in skip: continue
        t.SetWidth(tgt); n += 1
print('widened', n)
b.Save(sys.argv[1])
