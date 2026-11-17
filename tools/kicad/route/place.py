# Placement v1 for the backpack. usage: place.py in.kicad_pcb out.kicad_pcb
import pcbnew, sys, itertools
P = {
# --- E: RTD ---
'R23':(117.6,113.3,180),'U4':(117.6,117.6,-90),'C12':(113.5,116.6,-90),
'C13':(121.5,116.4,90),'C14':(121.5,119.5,90),'R24':(122.0,122.6,90),
'JP2':(114.2,123.2,90),
'R25':(117.6,126.0,180),'U5':(117.6,130.2,-90),'C15':(113.5,129.2,-90),
'C16':(121.5,128.9,90),'C17':(121.5,132.0,90),'R26':(122.0,135.1,90),
'JP3':(114.2,135.8,90),
# --- F: 1-Wire ---
'F4':(114.1,139.3,180),'D16':(113.5,142.4,0),'R27':(115.8,142.4,0),'R28':(115.8,144.3,0),
'D3':(114.3,146.6,0),'R2':(114.3,148.4,0),
# --- buck ---
'L1':(120.6,142.8,-90),'U1':(127.9,142.1,180),'C7':(124.7,142.9,90),
'C5':(128.4,139.0,180),'C6':(130.9,141.9,90),'C8':(121.2,148.0,0),'C9':(126.3,147.6,0),
'C4':(127.5,134.2,0),'JP1':(124.3,128.4,90),
# --- C: power outputs ---
}
def chan_out(n, q, d, rg, rp, led, rl):
    cx = 125.1 + 5.25*(n-1)
    P.update({q:(cx,114.3,90), d:(cx,117.6,0), rg:(cx-1.0,120.6,90), rp:(cx+1.0,120.6,90),
              led:(cx-1.0,123.8,90), rl:(cx+1.0,123.8,90)})
chan_out(1,'Q2','D4','R3','R4','D5','R5')
chan_out(2,'Q3','D6','R6','R7','D7','R8')
chan_out(3,'Q4','D8','R9','R10','D9','R11')
chan_out(4,'Q5','D10','R12','R13','D11','R14')
def chan_pwr(x0, q, d, f, u, rin, rg, rpd, led, rl, c):
    P.update({d:(x0+1.8,116.1,-90), q:(x0+7.2,117.4,90), rpd:(x0+0.9,121.2,90), led:(x0+2.7,121.2,90),
              f:(x0+5.3,127.5,0), u:(x0+2.3,132.8,0), rg:(x0+5.4,132.8,90), rin:(x0+7.2,132.8,90),
              c:(x0+9.4,132.8,90), rl:(x0+2.3,136.0,0)})
chan_pwr(143.6,'Q6','D12','F2','U2','R15','R16','R17','D13','R18','C10')
chan_pwr(154.5,'Q7','D14','F3','U3','R19','R20','R21','D15','R22','C11')
P.update({
# --- B: 12 V entry ---
'F1':(170.6,115.9,-90),'Q1':(168.5,134.0,90),'D2':(160.6,139.0,0),
'D1':(166.2,142.1,0),'R1':(162.1,142.3,0),'TP1':(148.5,137.9,0),
# --- G: front panel, level, UART, test points ---
'U6':(156.0,145.3,-90),'C19':(161.1,147.5,90),
'J7':(170.29,156.0,90),
'Q8':(164.0,145.2,0),'Q9':(164.0,148.6,0),'R33':(167.0,145.4,90),'R37':(167.0,148.6,90),
'R34':(166.6,152.2,90),'R38':(167.0,156.4,90),
'R31':(145.9,140.2,0),'R35':(145.9,142.0,0),'R32':(145.9,143.8,0),'R36':(145.9,145.6,0),
'D18':(149.5,140.2,0),'D19':(149.5,142.0,0),
'C1':(152.6,139.4,0),'C2':(155.4,139.7,90),'C3':(121.5,151.6,180),
'R29':(175.1,143.1,-90),'R30':(176.9,143.1,-90),'C18':(145.9,147.6,0),'D17':(175.6,145.2,180),
'J9':(161.51,156.9,-90),
'TP2':(125.6,157.4,0),'TP4':(128.8,157.4,0),'TP3':(113.4,150.4,0),
'TP5':(143.6,157.4,0),'TP6':(146.8,157.4,0),'TP7':(150.0,157.4,0),'TP8':(153.2,157.4,0),
})
b = pcbnew.LoadBoard(sys.argv[1])
fps = {f.GetReference(): f for f in b.GetFootprints()}
for r,(x,y,a) in P.items():
    f = fps[r]
    if f.IsLocked(): print('locked, skipped', r); continue
    f.SetOrientationDegrees(a); f.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
missing = [r for r,f in fps.items() if r not in P and not f.IsLocked()]
print('not placed:', missing)
# courtyard overlap check (front side)
def crt(f):
    c = f.GetCourtyard(pcbnew.B_CrtYd if f.IsFlipped() else pcbnew.F_CrtYd)
    return None if c.IsEmpty() else c
items = [(r,f,crt(f)) for r,f in fps.items()]
for (r1,f1,c1),(r2,f2,c2) in itertools.combinations(items,2):
    if c1 is None or c2 is None or f1.IsFlipped()!=f2.IsFlipped(): continue
    if c1.BBox().Intersects(c2.BBox()) and c1.Collide(c2): print('overlap', r1, r2)
b.Save(sys.argv[2])
