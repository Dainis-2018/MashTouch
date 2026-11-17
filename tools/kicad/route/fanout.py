# GND fan-out: a via next to every SMD GND pad (locked), so the routers can ignore GND. usage: fanout.py board
import pcbnew, sys, math
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.FromMM; tomm = pcbnew.ToMM
F, B = b.GetLayerID('F.Cu'), b.GetLayerID('B.Cu'); gnd = b.FindNet('GND')
VR, CLR, SW = 0.3, 0.27, 0.4
POWER = {'+12V','/+12V_IN','/+12V_F','+5V','/OUT_V','/BUCK_SW','/PWR1_P','/PWR1_L','/PWR2_P','/PWR2_L'}
clr = lambda n: CLR          # via radius (0.6/0.3), clearance with margin, stub width
rects = []   # (x1,y1,x2,y2,net)
for f in b.GetFootprints():
    for p in f.Pads():
        bb = p.GetBoundingBox(); rects.append((tomm(bb.GetLeft()),tomm(bb.GetTop()),tomm(bb.GetRight()),tomm(bb.GetBottom()),p.GetNetname(),p))
segs = []    # (x1,y1,x2,y2,halfwidth,net)
for t in b.GetTracks():
    if t.GetClass() == 'PCB_VIA':
        x,y = tomm(t.GetPosition().x), tomm(t.GetPosition().y); r = tomm(t.GetWidth(F))/2; segs.append((x,y,x,y,r,t.GetNetname()))
    else:
        segs.append((tomm(t.GetStart().x),tomm(t.GetStart().y),tomm(t.GetEnd().x),tomm(t.GetEnd().y),tomm(t.GetWidth())/2,t.GetNetname()))
def d_pt_rect(x,y,r): return math.hypot(max(r[0]-x,0,x-r[2]), max(r[1]-y,0,y-r[3]))
def d_pt_seg(x,y,s):
    ax,ay,bx,by=s[:4]; dx,dy=bx-ax,by-ay; L=dx*dx+dy*dy
    t=0 if L==0 else max(0,min(1,((x-ax)*dx+(y-ay)*dy)/L)); return math.hypot(x-ax-t*dx,y-ay-t*dy)
def seg_rect_d(ax,ay,bx,by,r,n=12):
    return min(d_pt_rect(ax+(bx-ax)*i/n, ay+(by-ay)*i/n, r) for i in range(n+1))
def seg_seg_d(ax,ay,bx,by,s,n=12):
    return min(d_pt_seg(ax+(bx-ax)*i/n, ay+(by-ay)*i/n, s) for i in range(n+1))
def free(px,py,vx,vy,own):
    if not (100.5+VR < vx < 189.5-VR and 100.5+VR < vy < 158.5-VR): return False
    if 171.5 < vx and 114.5 < vy < 142.5: return False
    for r in rects:
        if r[4]=='/GND' or r[4]=='GND':
            if r[5] is own: continue
            if d_pt_rect(vx,vy,r) < VR+0.2: return False   # keep vias off other GND pads too
            continue
        if d_pt_rect(vx,vy,r) < VR+clr(r[4]): return False
        if seg_rect_d(px,py,vx,vy,r) < SW/2+clr(r[4]): return False
    for s in segs:
        if s[5]=='GND': 
            if d_pt_seg(vx,vy,s) < VR+s[4]+0.25 and s[0]!=s[2]: pass
            continue
        if d_pt_seg(vx,vy,s) < VR+s[4]+clr(s[5]): return False
        if seg_seg_d(px,py,vx,vy,s) < SW/2+s[4]+clr(s[5]): return False
    # hole-to-hole with existing vias
    for s in segs:
        if s[0]==s[2] and s[1]==s[3] and math.hypot(vx-s[0],vy-s[1]) < 0.9: return False
    return True
def add(px,py,vx,vy):
    v=pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(mm(vx),mm(vy))); v.SetWidth(mm(2*VR)); v.SetDrill(mm(0.3))
    v.SetLayerPair(F,B); v.SetNet(gnd); v.SetLocked(True); b.Add(v)
    t=pcbnew.PCB_TRACK(b); t.SetStart(pcbnew.VECTOR2I(mm(px),mm(py))); t.SetEnd(pcbnew.VECTOR2I(mm(vx),mm(vy)))
    t.SetWidth(mm(SW)); t.SetLayer(F); t.SetNet(gnd); t.SetLocked(True); b.Add(t)
    segs.append((vx,vy,vx,vy,VR,'GND'))
done=0; failed=[]
for f in b.GetFootprints():
    for p in f.Pads():
        if p.GetNetname()!='GND' or p.GetAttribute()!=pcbnew.PAD_ATTRIB_SMD: continue
        px,py=tomm(p.GetPosition().x),tomm(p.GetPosition().y)
        bb=p.GetBoundingBox(); hw,hh=tomm(bb.GetWidth())/2,tomm(bb.GetHeight())/2
        # already next to a GND via?
        if any(s[5]=='GND' and s[0]==s[2] and abs(s[0]-px)<=hw+0.1 and abs(s[1]-py)<=hh+0.1 for s in segs): done+=1; continue
        ok=False
        for ext in (0.45,0.7,1.0,1.4,1.9):
            for ang in range(0,360,30):
                a=math.radians(ang); ux,uy=math.cos(a),math.sin(a)
                k=min(hw/abs(ux) if abs(ux)>1e-6 else 1e9, hh/abs(uy) if abs(uy)>1e-6 else 1e9)
                vx,vy=px+ux*(k+ext),py+uy*(k+ext)
                if free(px,py,vx,vy,p): add(px,py,vx,vy); ok=True; break
            if ok: break
        if ok: done+=1
        else: failed.append(f.GetReference()+'.'+p.GetNumber())
print('fanned out', done, 'failed', failed)
b.Save(sys.argv[1])
