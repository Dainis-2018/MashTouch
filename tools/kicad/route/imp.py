# import a Freerouting session, keeping the GND fan-out (Freerouting drops GND). usage: imp.py board ses out
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
keep = []
for t in b.GetTracks():
    if t.GetNetname() == 'GND':
        if t.GetClass() == 'PCB_VIA': keep.append(('v', t.GetPosition(), t.GetWidth(pcbnew.F_Cu), t.GetDrillValue()))
        else: keep.append(('t', t.GetStart(), t.GetEnd(), t.GetWidth(), t.GetLayer()))
print('import', pcbnew.ImportSpecctraSES(b, sys.argv[2]))
have = set((t.GetPosition().x, t.GetPosition().y) for t in b.GetTracks() if t.GetClass()=='PCB_VIA' and t.GetNetname()=='GND')
g = b.FindNet('GND'); n = 0
for k in keep:
    if k[0] == 'v':
        if (k[1].x, k[1].y) in have: continue
        v = pcbnew.PCB_VIA(b); v.SetPosition(k[1]); v.SetWidth(k[2]); v.SetDrill(k[3]); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNet(g); v.SetLocked(True); b.Add(v); n += 1
    else:
        t = pcbnew.PCB_TRACK(b); t.SetStart(k[1]); t.SetEnd(k[2]); t.SetWidth(k[3]); t.SetLayer(k[4]); t.SetNet(g); t.SetLocked(True); b.Add(t); n += 1
print('re-added GND items', n, 'of', len(keep))
b.Save(sys.argv[3])
