# Hand-routed power paths (locked), GND pours and the notch keepout. usage: hand.py board
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
mm = pcbnew.FromMM
V = lambda x, y: pcbnew.VECTOR2I(mm(x), mm(y))
F, B = b.GetLayerID('F.Cu'), b.GetLayerID('B.Cu')
# start clean: remove tracks, vias and zones from earlier runs
for t in list(b.GetTracks()): b.Delete(t)
for z in list(b.Zones()): b.Delete(z)
ADD_ZONES = len(sys.argv) > 2
net = lambda n: b.FindNet(n)
def T(n, w, layer, *pts):
    for a, c in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b); t.SetStart(V(*a)); t.SetEnd(V(*c)); t.SetWidth(mm(w)); t.SetLayer(layer)
        t.SetNet(net(n)); t.SetLocked(True); b.Add(t)
def VIA(n, x, y, d=0.8, drill=0.4):
    v = pcbnew.PCB_VIA(b); v.SetPosition(V(x, y)); v.SetWidth(mm(d)); v.SetDrill(mm(drill))
    v.SetLayerPair(F, B); v.SetNet(net(n)); v.SetLocked(True); b.Add(v)
# 12 V input: J3 pin 2 -> F1 -> Q1 tab
T('/+12V_IN', 3.0, F, (167.75,108.84), (167.2,115.9)); T('/+12V_IN', 2.0, F, (167.2,115.9), (170.6,115.9))
T('/+12V_F', 2.5, F, (167.2,125.82), (167.7,130.0)); T('/+12V_F', 2.5, F, (170.6,125.82), (169.4,130.0))
# +12V trunk: Q1 source -> vias -> back layer along y 137.6 -> up to the PTCs F2/F3
for x in (169.9, 170.8, 171.6): VIA('+12V', x, 140.75)
T('+12V', 1.2, F, (170.78,139.04), (170.8,140.75)); T('+12V', 0.8, F, (169.9,140.75), (171.6,140.75))
T('+12V', 2.0, B, (170.8,140.75), (167.95,137.6)); T('+12V', 3.0, B, (167.95,137.6), (156.4,137.6)); T('+12V', 2.0, B, (156.4,137.6), (145.5,137.6), (145.5,131.5))
T('+12V', 2.0, B, (156.4,137.6), (156.4,131.5))
for x0 in (145.5, 156.4):   # F2 / F3 pad 1, plus driver VDD (pin 1)
    VIA('+12V', x0, 130.75); VIA('+12V', x0+0.9, 130.75)
    T('+12V', 1.2, B, (x0,130.75), (x0,131.5)); T('+12V', 0.8, B, (x0,130.75), (x0+0.9,130.75))
    T('+12V', 1.5, F, (x0+0.01,129.5), (x0,130.75)); T('+12V', 0.6, F, (x0,130.75), (x0+0.9,130.75))
    T('+12V', 0.5, F, (x0-0.74,131.85), (x0,130.75))
# PWR1: PTC F2 -> P terminal (back layer), flyback D12, MOSFET Q6 tab -> L terminal
for x in (151.45, 152.35): VIA('/PWR1_P', x, 124.45)
T('/PWR1_P', 2.0, B, (151.9,124.45), (150.25,119.5), (150.25,108.84))
T('/PWR1_P', 0.8, B, (151.45,124.45), (152.35,124.45))
T('/PWR1_P', 1.0, F, (145.4,114.1), (145.4,111.9), (150.25,111.9), (150.25,108.84))
T('/PWR1_L', 2.0, F, (152.8,113.6), (153.75,108.84))
T('/PWR1_L', 1.2, F, (145.4,118.1), (148.5,118.1))
# PWR2
for x in (162.75, 163.65): VIA('/PWR2_P', x, 124.45)
T('/PWR2_P', 2.0, B, (163.2,124.45), (157.25,118.5), (157.25,108.84))
T('/PWR2_P', 0.8, B, (162.75,124.45), (163.65,124.45))
T('/PWR2_P', 1.0, F, (156.3,114.1), (156.3,112.0), (157.25,110.6))
T('/PWR2_L', 2.0, F, (161.2,113.6), (160.75,108.84))
T('/PWR2_L', 1.2, F, (156.3,118.1), (159.4,118.1))
# MOSFET sources to ground: two vias each
for (sx, vx) in ((153.08, 153.95), (163.98, 164.85)):
    VIA('GND', vx, 121.7); VIA('GND', vx, 123.2)
    T('GND', 0.8, F, (sx,122.44), (vx,122.44)); T('GND', 0.8, F, (vx,121.7), (vx,123.2))
    if vx < 160: VIA('GND', vx, 124.7); T('GND', 0.8, F, (vx,123.2), (vx,124.7))
    else: VIA('GND', vx+0.9, 122.45); T('GND', 0.8, F, (vx,122.44), (vx+0.9,122.45))

# 12 V to the buck: trunk extended west on the back layer, via next to C5
T('+12V', 1.0, B, (145.5,137.6), (132.2,137.6), (131.4,138.4)); VIA('+12V', 131.4, 138.4); T('+12V', 0.8, F, (131.4,138.4), (130.2,139.0))
# buck: switch node, bootstrap, input and 5 V output kept short (U1 left pins 4/5/6 at x 126.76)
T('/BUCK_SW', 0.45, F, (126.76,142.1), (125.4,142.1)); T('/BUCK_SW', 0.8, F, (125.4,142.1), (124.7,142.12), (123.0,141.3))
T('/BUCK_BST', 0.4, F, (126.76,143.05), (125.4,143.05), (124.7,143.68))
T('+12V', 0.6, F, (129.04,141.15), (129.04,142.1)); T('+12V', 0.6, F, (129.04,141.15), (129.88,139.6))
T('+12V', 0.4, F, (129.04,142.1), (129.9,142.1), (130.9,142.68))
T('GND', 0.6, F, (126.76,141.15), (126.92,139.3)); VIA('GND', 125.75, 138.4, 0.6, 0.3); T('GND', 0.5, F, (125.75,138.4), (126.92,138.9))
T('+5V', 1.0, F, (119.7,145.2), (119.7,147.9))
T('+5V', 1.0, F, (122.6,145.4), (124.82,147.4), (124.82,147.9), (123.0,150.9), (123.0,151.6), (125.95,151.21))

# RTD front ends (MAX31865 U4/U5): short direct tracks, filter cap on RTD+/RTD-, FORCE2 to the jumper on the back
def rtd(ch, dy, dj, rdy):
    n = lambda s: '/RTD%d_%s' % (ch, s)
    jx = 110.64                      # right end of J4's pads
    yFP, ySP, ySM, yFM = 110.9+dj, 114.4+dj, 117.9+dj, 121.4+dj
    w = 0.25
    T(n('SP'), w, F, (108.84,ySP), (111.9,ySP), (112.9,115.4+dy))
    T(n('SP'), w, F, (114.0,115.95+dy), (114.5,116.95+dy), (115.16,116.95+dy))
    T(n('SM'), w, F, (108.84,ySM), (111.6,ySM), (112.9,117.55+dy))
    T(n('SM'), w, F, (114.0,117.6+dy), (115.16,117.6+dy))
    T(n('FM'), w, F, (108.84,yFM), (111.8,yFM), (113.9,119.4+dy), (114.5,118.4+dy), (115.16,118.25+dy))
    T(n('FP'), w, F, (108.84,yFP), (112.0,yFP), (113.6,112.5+dy), (115.0,113.9+dy), (115.9,114.4+dy), (116.3,114.8+dy), (116.3,115.16+dy))
    T(n('FP'), w, B, (108.84,yFP), (111.4,yFP+0.8), (111.4,121.5+dy), (112.9,123.0+dy), (114.2,123.2+dy))
    VIA(n('F2'), 114.75, 114.7+dy, 0.6, 0.3)
    T(n('F2'), w, F, (115.16,116.3+dy), (114.75,116.0+dy), (114.75,114.7+dy))
    T(n('F2'), w, B, (114.75,114.7+dy), (114.0,115.5+dy), (114.0,120.6+dy), (116.74,122.0+dy), (116.74,123.2+dy))
    for xs, net, xr in (((116.95,117.6), 'REF', 116.95), ((118.25,118.9), 'BIAS', 118.51)):
        yb = 114.4+dy
        for x in xs: T(n(net), 0.2, F, (x,115.16+dy), (x,yb))
        T(n(net), 0.2, F, (xs[0],yb), (xs[1],yb)); T(n(net), 0.3, F, (xr,yb), (xr,113.3+rdy+0.5))
rtd(1, 0, 0, 0); rtd(2, 12.6, 14.0, 12.7)
# D1 (Q1 gate clamp) to +12V
T('+12V', 0.5, F, (169.9,140.75), (164.9,140.75), (164.55,141.5))

# level input: LEVEL_IN joined along the top, 3V3 out through the strip under the notch, LEVEL on the back to J8 pin 18
T('/LEVEL_IN', 0.3, F, (175.1,142.28), (176.9,142.28), (179.6,142.28), (181.16,143.6))
T('/LEVEL_IN', 0.25, F, (176.0,142.28), (176.0,145.2))
T('+3V3', 0.3, F, (175.1,143.92), (174.2,143.92), (173.8,143.3), (169.2,143.3))
T('/LEVEL', 0.25, F, (176.9,143.92), (177.6,144.6), (177.6,144.9)); VIA('/LEVEL', 177.6, 144.9, 0.6, 0.3)
T('/LEVEL', 0.25, B, (177.6,144.9), (177.6,157.0), (176.9,157.7), (169.0,157.7), (167.4,156.1), (166.6,155.3), (147.54,155.3), (147.54,152.4), (146.27,151.21))
# zones: GND on both layers over the whole board, no copper next to the antenna notch
def poly(z, pts):
    o = z.Outline(); o.NewOutline()
    for x, y in pts: o.Append(mm(x), mm(y))
for layer, prio in (((B, 0), (F, 0)) if ADD_ZONES else ()):
    z = pcbnew.ZONE(b); z.SetLayer(layer); z.SetNet(net('GND')); z.SetAssignedPriority(prio)
    z.SetLocalClearance(mm(0.3)); z.SetMinThickness(mm(0.25)); z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.5))
    poly(z, [(100,100),(190,100),(190,159),(100,159)]); b.Add(z)
k = pcbnew.ZONE(b); k.SetIsRuleArea(True); k.SetLayerSet(pcbnew.LSET.AllCuMask())
k.SetDoNotAllowZoneFills(True); k.SetDoNotAllowTracks(True); k.SetDoNotAllowVias(True); k.SetDoNotAllowPads(False); k.SetDoNotAllowFootprints(False)
k.SetZoneName('antenna'); poly(k, [(172,115),(190,115),(190,142),(172,142)]); b.Add(k)

for i,(x1,y1,x2,y2) in enumerate(((104,100,186,100.6),(104,158.4,186,159),(100,104,100.6,155),(189.4,104,190,115),(189.4,142,190,155))):
    e = pcbnew.ZONE(b); e.SetIsRuleArea(True); e.SetLayerSet(pcbnew.LSET.AllCuMask())
    e.SetDoNotAllowZoneFills(False); e.SetDoNotAllowTracks(True); e.SetDoNotAllowVias(True); e.SetDoNotAllowPads(False); e.SetDoNotAllowFootprints(False)
    e.SetZoneName('edge%d'%i); poly(e, [(x1,y1),(x2,y1),(x2,y2),(x1,y2)]); b.Add(e)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(sys.argv[1])
