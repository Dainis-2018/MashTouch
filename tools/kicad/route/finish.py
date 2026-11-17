# finish.py board: drop vias connected on one layer only, add GND stitching vias, refill
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.FromMM
F, B = pcbnew.F_Cu, pcbnew.B_Cu
# vias of signal nets that touch a track on one layer only
ends = {}
for t in b.GetTracks():
    if t.GetClass() == 'PCB_TRACK':
        for p in (t.GetStart(), t.GetEnd()): ends.setdefault((p.x, p.y, t.GetLayer()), 0); ends[(p.x, p.y, t.GetLayer())] += 1
pads = [(p.GetPosition(), p) for f in b.GetFootprints() for p in f.Pads()]
dropped = 0
for v in [t for t in b.GetTracks() if t.GetClass() == 'PCB_VIA' and t.GetNetname() != 'GND' and not t.IsLocked()]:
    pos = v.GetPosition()
    same = [t for t in b.GetTracks() if t.GetClass() == 'PCB_TRACK' and t.GetNetCode() == v.GetNetCode() and t.HitTest(pos, 0)]
    onF = any(t.GetLayer() == F for t in same) or any(p.HitTest(pos) and p.IsOnLayer(F) and p.GetNetCode() == v.GetNetCode() for _, p in pads)
    onB = any(t.GetLayer() == B for t in same) or any(p.HitTest(pos) and p.IsOnLayer(B) and p.GetNetCode() == v.GetNetCode() for _, p in pads)
    if not (onF and onB): b.Delete(v); dropped += 1
print('dropped one-sided vias', dropped)
filler = pcbnew.ZONE_FILLER(b); zones = [z for z in b.Zones() if not z.GetIsRuleArea()]
filler.Fill(zones)
zf = [z for z in zones if z.GetLayer() == F][0]; zb = [z for z in zones if z.GetLayer() == B][0]
vias = [t.GetPosition() for t in b.GetTracks() if t.GetClass() == 'PCB_VIA']
holes = [p.GetPosition() for _, p in pads if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH)]
added = 0; g = b.FindNet('GND')
def stitch(step, off, margin):
    global added
    y = 101.5 + off
    while y < 158:
        x = 101.5 + off
        while x < 189:
            p = pcbnew.VECTOR2I(mm(x), mm(y)); ok = True
            for dx, dy in ((0,0),(margin,0),(-margin,0),(0,margin),(0,-margin),(margin*.72,margin*.72),(-margin*.72,margin*.72),(margin*.72,-margin*.72),(-margin*.72,-margin*.72)):
                q = pcbnew.VECTOR2I(mm(x+dx), mm(y+dy))
                if not (zf.HitTestFilledArea(F, q) and zb.HitTestFilledArea(B, q)): ok = False; break
            if ok and not (x > 171.4 and 114.6 < y < 142.4) and all((p - v).EuclideanNorm() > mm(0.95) for v in vias) and all((p - h).EuclideanNorm() > mm(1.4) for h in holes):
                v = pcbnew.PCB_VIA(b); v.SetPosition(p); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetLayerPair(F, B); v.SetNet(g); b.Add(v)
                vias.append(p); added += 1
            x += step
        y += step
stitch(2.5, 0, 0.5)
filler.Fill(zones)
# islands without any GND via or THT GND pad: try to drop one via inside each
gnd_pts = [t.GetPosition() for t in b.GetTracks() if t.GetClass()=='PCB_VIA' and t.GetNetname()=='GND']
gnd_pts += [p.GetPosition() for _, p in pads if p.GetNetname()=='GND' and p.GetAttribute()==pcbnew.PAD_ATTRIB_PTH]
for z, L, other in ((zf, F, zb), (zb, B, zf)):
    polys = z.GetFilledPolysList(L)
    for i in range(polys.OutlineCount()):
        isl = pcbnew.SHAPE_POLY_SET(); isl.AddOutline(polys.Outline(i))
        if any(isl.Contains(q) for q in gnd_pts): continue
        bb = polys.Outline(i).BBox(); done = False
        yy = bb.GetTop()
        while yy <= bb.GetBottom() and not done:
            xx = bb.GetLeft()
            while xx <= bb.GetRight() and not done:
                p = pcbnew.VECTOR2I(int(xx), int(yy)); ok = True
                for dx, dy in ((0,0),(0.4,0),(-0.4,0),(0,0.4),(0,-0.4),(0.29,0.29),(-0.29,0.29),(0.29,-0.29),(-0.29,-0.29)):
                    q = pcbnew.VECTOR2I(int(xx+mm(dx)), int(yy+mm(dy)))
                    if not (z.HitTestFilledArea(L, q) and other.HitTestFilledArea(other.GetLayer(), q)): ok = False; break
                if ok and all((p - h).EuclideanNorm() > mm(1.4) for h in holes):
                    v = pcbnew.PCB_VIA(b); v.SetPosition(p); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetLayerPair(F, B); v.SetNet(g); b.Add(v)
                    gnd_pts.append(p); added += 1; done = True
                xx += mm(0.2)
            yy += mm(0.2)
        if not done: print('island without room for a via at', round(pcbnew.ToMM(bb.Centre().x),2), round(pcbnew.ToMM(bb.Centre().y),2), 'layer', L)
print('stitching vias', added)
filler.Fill(zones)
b.Save(sys.argv[1])
