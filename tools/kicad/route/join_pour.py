# join_pour.py board GND x1 y1 x2 y2 out: link GND copper of a stray island group (points along the given segment)
# to the nearest point of the main GND pour with a straight F.Cu track (or B.Cu track plus via), clearance-checked
import pcbnew, sys, math
exec(open(__file__.replace('join_pour.py','connect.py')).read().split("best=None")[0])
zl = {z.GetLayer(): z for z in b.Zones() if not z.GetIsRuleArea()}
x1,y1,x2,y2 = map(float, sys.argv[3:7]); out = sys.argv[7]
stray = pcbnew.SHAPE_POLY_SET()
for L in (F, B):
    ps = zl[L].GetFilledPolysList(L)
    for i in range(ps.OutlineCount()):
        bb = ps.Outline(i).BBox()
        if bb.GetWidth() < mm(14) and bb.GetHeight() < mm(14) and bb.Contains(pcbnew.VECTOR2I(mm((x1+x2)/2), mm((y1+y2)/2))): pass
cands = []
for L in (F, B):
    ps = zl[L].GetFilledPolysList(L)
    big = [i for i in range(ps.OutlineCount()) if ps.Outline(i).Area() > 2000e12]
    yy = 133.0
    while yy < 152:
        xx = 142.0
        while xx < 164:
            p = pcbnew.VECTOR2I(mm(xx), mm(yy))
            if any(ps.Outline(i).PointInside(p) for i in big) and all(zl[L].HitTestFilledArea(L, pcbnew.VECTOR2I(mm(xx+dx), mm(yy+dy))) for dx,dy in ((0.3,0),(-0.3,0),(0,0.3),(0,-0.3))):
                cands.append((L, xx, yy))
            xx += 0.25
        yy += 0.25
print('main-pour candidate points', len(cands))
best = None
for i in range(0, 13):
    sx, sy = x1+(x2-x1)*i/12, y1+(y2-y1)*i/12
    for (L, tx, ty) in cands:
        d = math.hypot(tx-sx, ty-sy) + (1.0 if L == B else 0)
        if best and d >= best[0]: continue
        if L == B and not ptclear(sx, sy, VR): continue
        if segclear(L, sx, sy, tx, ty, W/2): best = (d, L, sx, sy, tx, ty)
print('best', best)
if best:
    _, L, sx, sy, tx, ty = best
    t = pcbnew.PCB_TRACK(b); t.SetStart(pcbnew.VECTOR2I(mm(sx),mm(sy))); t.SetEnd(pcbnew.VECTOR2I(mm(tx),mm(ty))); t.SetWidth(mm(W)); t.SetLayer(L); t.SetNet(b.FindNet('GND')); b.Add(t)
    if L == B:
        v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(mm(sx),mm(sy))); v.SetWidth(mm(2*VR)); v.SetDrill(mm(0.3)); v.SetLayerPair(F,B); v.SetNet(b.FindNet('GND')); b.Add(v)
    pcbnew.ZONE_FILLER(b).Fill([z for z in b.Zones() if not z.GetIsRuleArea()]); b.Save(out)
