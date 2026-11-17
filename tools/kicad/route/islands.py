# islands.py board: join GND pour islands that are not connected to the main ground, with stitching vias
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.FromMM
F, B = pcbnew.F_Cu, pcbnew.B_Cu; g = b.FindNet('GND')
zones = [z for z in b.Zones() if not z.GetIsRuleArea()]
zl = {z.GetLayer(): z for z in zones}
filler = pcbnew.ZONE_FILLER(b)
holes = [p.GetPosition() for f in b.GetFootprints() for p in f.Pads() if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH)]
for it in range(6):
    filler.Fill(zones)
    isl = []   # (layer, outline poly set)
    for L in (F, B):
        ps = zl[L].GetFilledPolysList(L)
        for i in range(ps.OutlineCount()):
            s = pcbnew.SHAPE_POLY_SET(); s.AddOutline(ps.Outline(i)); isl.append((L, s))
    par = list(range(len(isl)))
    def find(a):
        while par[a] != a: par[a] = par[par[a]]; a = par[a]
        return a
    def union(a, c): par[find(a)] = find(c)
    def at(L, p): return [i for i, (l, s) in enumerate(isl) if l == L and s.Contains(p)]
    links = [t.GetPosition() for t in b.GetTracks() if t.GetClass()=='PCB_VIA' and t.GetNetname()=='GND']
    links += [p.GetPosition() for f in b.GetFootprints() for p in f.Pads() if p.GetNetname()=='GND' and p.GetAttribute()==pcbnew.PAD_ATTRIB_PTH]
    for p in links:
        ids = at(F, p) + at(B, p)
        for a in ids[1:]: union(a, ids[0])
    # GND tracks and SMD pads join the F islands they touch
    for t in b.GetTracks():
        if t.GetNetname()=='GND' and t.GetClass()=='PCB_TRACK':
            ids = at(t.GetLayer(), t.GetStart()) + at(t.GetLayer(), t.GetEnd())
            for a in ids[1:]: union(a, ids[0])
    groups = {}
    for i in range(len(isl)): groups.setdefault(find(i), []).append(i)
    area = lambda gi: sum(isl[i][1].Area() for i in groups[gi])
    main = max(groups, key=area)
    stray = [gi for gi in groups if gi != main]
    print('pass', it, 'islands', len(isl), 'groups', len(groups))
    if not stray: break
    vias = list(links); added = 0
    for gi in stray:
        done = False
        for i in groups[gi]:
            L, s = isl[i]; other = B if L == F else F; bb = s.BBox()
            yy = bb.GetTop()
            while yy <= bb.GetBottom() and not done:
                xx = bb.GetLeft()
                while xx <= bb.GetRight() and not done:
                    p = pcbnew.VECTOR2I(int(xx), int(yy))
                    okp = all(zl[L].HitTestFilledArea(L, pcbnew.VECTOR2I(int(xx+mm(dx)), int(yy+mm(dy)))) and zl[other].HitTestFilledArea(other, pcbnew.VECTOR2I(int(xx+mm(dx)), int(yy+mm(dy))))
                              for dx, dy in ((0,0),(0.25,0),(-0.25,0),(0,0.25),(0,-0.25),(0.18,0.18),(-0.18,0.18),(0.18,-0.18),(-0.18,-0.18)))
                    if okp and s.Contains(p) and any(find(j) == main for j in at(other, p)) and all((p-h).EuclideanNorm() > mm(1.4) for h in holes) and not (pcbnew.ToMM(p.x) > 171.4 and 114.6 < pcbnew.ToMM(p.y) < 142.4) and all((p-v).EuclideanNorm() > mm(0.9) for v in vias):
                        v = pcbnew.PCB_VIA(b); v.SetPosition(p); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetLayerPair(F, B); v.SetNet(g); b.Add(v)
                        vias.append(p); added += 1; done = True
                    xx += mm(0.1)
                yy += mm(0.1)
            if done: break
        if not done:
            c = isl[groups[gi][0]][1].BBox().Centre(); print('  no spot for group near', round(pcbnew.ToMM(c.x),1), round(pcbnew.ToMM(c.y),1))
    print('  added', added)
    if not added: break
filler.Fill(zones)
b.Save(sys.argv[1])
