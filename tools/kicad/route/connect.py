# connect.py board net x1 y1 x2 y2: find a clear straight link (F.Cu, or B.Cu with vias) from a point on segment (x1,y1)-(x2,y2) to the net's other copper
import pcbnew, sys, math
b = pcbnew.LoadBoard(sys.argv[1]); net = sys.argv[2]; mm = pcbnew.FromMM; tomm = pcbnew.ToMM
x1,y1,x2,y2 = map(float, sys.argv[3:7]); W = float(__import__("os").environ.get("LW","0.3")); CL = 0.27; VR = 0.3
F, B = pcbnew.F_Cu, pcbnew.B_Cu
obs = {F: [], B: []}; targets = {F: [], B: []}
def rect(bb): return (tomm(bb.GetLeft()), tomm(bb.GetTop()), tomm(bb.GetRight()), tomm(bb.GetBottom()))
for f in b.GetFootprints():
    for p in f.Pads():
        for L in (F, B):
            if p.IsOnLayer(L):
                (targets if p.GetNetname() == net else obs)[L].append(('r', rect(p.GetBoundingBox())))
for t in b.GetTracks():
    if t.GetClass() == 'PCB_VIA':
        x, y = tomm(t.GetPosition().x), tomm(t.GetPosition().y); r = tomm(t.GetWidth(F))/2
        for L in (F, B): (targets if t.GetNetname() == net else obs)[L].append(('s', (x,y,x,y,r)))
    else:
        s = (tomm(t.GetStart().x), tomm(t.GetStart().y), tomm(t.GetEnd().x), tomm(t.GetEnd().y), tomm(t.GetWidth())/2)
        (targets if t.GetNetname() == net else obs)[t.GetLayer()].append(('s', s))
def dps(px,py,s):
    ax,ay,bx,by=s[:4]; dx,dy=bx-ax,by-ay; L=dx*dx+dy*dy; t=0 if L==0 else max(0,min(1,((px-ax)*dx+(py-ay)*dy)/L)); return math.hypot(px-ax-t*dx,py-ay-t*dy)
def dpr(px,py,r): return math.hypot(max(r[0]-px,0,px-r[2]), max(r[1]-py,0,py-r[3]))
def dist(px,py,o): return dpr(px,py,o[1]) if o[0]=='r' else dps(px,py,o[1])-o[1][4]
def segclear(L,ax,ay,bx,by,hw):
    n = max(2, int(math.hypot(bx-ax,by-ay)/0.05))
    for i in range(n+1):
        px,py = ax+(bx-ax)*i/n, ay+(by-ay)*i/n
        for o in obs[L]:
            if dist(px,py,o) < hw+CL: return False
    return True
def ptclear(px,py,r): return all(dist(px,py,o) >= r+CL for L in (F,B) for o in obs[L])
EX=[float(v) for v in __import__('os').environ.get('EXCL','0,0,0,0').split(',')]
def tpoints(L):
    for o in targets[L]:
        c=o[1]; cx,cy=((c[0]+c[2])/2,(c[1]+c[3])/2)
        if EX[0]<=cx<=EX[2] and EX[1]<=cy<=EX[3]: continue
        if o[0]=='r': r=o[1]; yield ((r[0]+r[2])/2,(r[1]+r[3])/2)
        else: yield (o[1][0],o[1][1]); yield (o[1][2],o[1][3])
best=None
for i in range(0, int(__import__('os').environ.get('NS','40'))+1):
    NSv=int(__import__('os').environ.get('NS','40')); sx, sy = x1+(x2-x1)*i/NSv, y1+(y2-y1)*i/NSv
    for (tx,ty) in tpoints(F):
        d=math.hypot(tx-sx,ty-sy)
        if d<0.5 or (best and d>=best[0]): continue
        if segclear(F,sx,sy,tx,ty,W/2): best=(d,'F',sx,sy,tx,ty)
    if ptclear(sx,sy,VR):
        for (tx,ty) in tpoints(B):
            d=math.hypot(tx-sx,ty-sy)+1.0
            if best and d>=best[0]: continue
            if segclear(B,sx,sy,tx,ty,W/2): best=(d,'B',sx,sy,tx,ty)
print('best', best)
if best and len(sys.argv) > 7:
    _,L,sx,sy,tx,ty = best; Ly = F if L=='F' else B
    t=pcbnew.PCB_TRACK(b); t.SetStart(pcbnew.VECTOR2I(mm(sx),mm(sy))); t.SetEnd(pcbnew.VECTOR2I(mm(tx),mm(ty))); t.SetWidth(mm(W)); t.SetLayer(Ly); t.SetNet(b.FindNet(net)); b.Add(t)
    if L=='B':
        v=pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(mm(sx),mm(sy))); v.SetWidth(mm(2*VR)); v.SetDrill(mm(0.3)); v.SetLayerPair(F,B); v.SetNet(b.FindNet(net)); b.Add(v)
    b.Save(sys.argv[7])
