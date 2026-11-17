# GND pads the automatic fan-out could not reach
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.FromMM; V = lambda x,y: pcbnew.VECTOR2I(mm(x),mm(y))
F, B = b.GetLayerID('F.Cu'), b.GetLayerID('B.Cu'); g = b.FindNet('GND')
fp = {f.GetReference(): f for f in b.GetFootprints()}
pad = lambda r,n: [(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)) for p in fp[r].Pads() if p.GetNumber()==n][0]
def T(w,*pts):
    for a,c in zip(pts,pts[1:]):
        t=pcbnew.PCB_TRACK(b); t.SetStart(V(*a)); t.SetEnd(V(*c)); t.SetWidth(mm(w)); t.SetLayer(F); t.SetNet(g); t.SetLocked(True); b.Add(t)
def VIA(x,y):
    v=pcbnew.PCB_VIA(b); v.SetPosition(V(x,y)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetLayerPair(F,B); v.SetNet(g); v.SetLocked(True); b.Add(v)
# MAX31865 exposed pads: four vias each
for r in ('U4','U5'):
    x,y = pad(r,'21')
    for dx,dy in ((-0.8,-0.8),(0.8,-0.8),(-0.8,0.8),(0.8,0.8)): VIA(x+dx,y+dy)
# TCA9535: GND pins 3, 12, 21 to vias under the body
for n,(dx,dy) in (('3',(0,2.3)),('12',(0,2.1)),('21',(0,-1.8))):
    x,y = pad('U6',n); VIA(x+dx,y+dy); T(0.3,(x,y),(x+dx,y+dy))
# drivers: pin 5 to pin 4; LED resistors to the driver's GND pin
for u,r in (('U2','R18'),('U3','R22')):
    T(0.5, pad(u,'5'), pad(u,'4')); T(0.4, pad(r,'2'), (pad(r,'2')[0], pad(u,'4')[1]+0.6), pad(u,'4'))
# 1-Wire ESD to J4's GND pin
x,y = pad('D16','2'); T(0.4,(x,y),(x,y+1.2),(112.6,145.9),pad('J4','11'))
# TVS, level ESD
x,y = pad('D2','2'); VIA(x,y+1.75); T(0.8,(x,y),(x,y+1.75))
x,y = pad('D17','2'); VIA(x-0.35,y+1.0); T(0.3,(x,y),(x-0.35,y+1.0))
# button ESD: one via between D18 and D19
VIA(150.6,141.1); T(0.3, pad('D18','2'), (150.6,141.1), pad('D19','2'))
# C2 to C1's ground pad; C1 gets a via below it (the 12 V trunk runs above)
T(0.4, pad('C2','2'), pad('C1','2'))
x,y = pad('C1','2'); VIA(x,y+1.35); T(0.4,(x,y),(x,y+1.35))
b.Save(sys.argv[1])
