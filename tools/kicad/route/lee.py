# lee.py board x1 y1 x2 y2 out [region x0 y0 x1 y1]: grid maze route (both layers, vias allowed) for GND
# from copper of a stray island group (points along the segment x1,y1-x2,y2) to the main GND pour
import pcbnew, sys, math, collections, os
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.FromMM; tomm = pcbnew.ToMM
F, B = pcbnew.F_Cu, pcbnew.B_Cu
sx1, sy1, sx2, sy2 = map(float, sys.argv[2:6]); out = sys.argv[6]
RX0, RY0, RX1, RY1 = map(float, (sys.argv[7:11] if len(sys.argv) > 10 else (140, 130, 168, 157)))
G = 0.2; W = 0.25; CL = 0.26; VR = 0.3
obs = {F: [], B: []}
def rect(bb): return ('r', (tomm(bb.GetLeft()), tomm(bb.GetTop()), tomm(bb.GetRight()), tomm(bb.GetBottom())))
inreg = lambda x, y: RX0-3 <= x <= RX1+3 and RY0-3 <= y <= RY1+3
for f in b.GetFootprints():
    for p in f.Pads():
        if p.GetNetname() == 'GND': continue
        o = rect(p.GetBoundingBox())
        if not inreg((o[1][0]+o[1][2])/2, (o[1][1]+o[1][3])/2): continue
        for L in (F, B):
            if p.IsOnLayer(L): obs[L].append(o)
        if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
            for L in (F, B): obs[L].append(o)
for t in b.GetTracks():
    if t.GetNetname() == 'GND': continue
    if t.GetClass() == 'PCB_VIA':
        x, y = tomm(t.GetPosition().x), tomm(t.GetPosition().y)
        if inreg(x, y):
            for L in (F, B): obs[L].append(('s', (x, y, x, y, tomm(t.GetWidth(F))/2)))
    else:
        s = (tomm(t.GetStart().x), tomm(t.GetStart().y), tomm(t.GetEnd().x), tomm(t.GetEnd().y), tomm(t.GetWidth())/2)
        if inreg(s[0], s[1]) or inreg(s[2], s[3]): obs[t.GetLayer()].append(('s', s))
holes = [p.GetPosition() for f in b.GetFootprints() for p in f.Pads() if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH)]
def dps(px, py, s):
    ax, ay, bx, by = s[:4]; dx, dy = bx-ax, by-ay; L = dx*dx+dy*dy
    t = 0 if L == 0 else max(0, min(1, ((px-ax)*dx+(py-ay)*dy)/L)); return math.hypot(px-ax-t*dx, py-ay-t*dy)
def dist(px, py, o):
    if o[0] == 'r': r = o[1]; return math.hypot(max(r[0]-px, 0, px-r[2]), max(r[1]-py, 0, py-r[3]))
    return dps(px, py, o[1]) - o[1][4]
zl = {z.GetLayer(): z for z in b.Zones() if not z.GetIsRuleArea()}
nx, ny = int((RX1-RX0)/G)+1, int((RY1-RY0)/G)+1
X = lambda i: RX0+i*G; Y = lambda j: RY0+j*G
free = {}; via_ok = {}
for L in (F, B):
    for i in range(nx):
        for j in range(ny):
            x, y = X(i), Y(j)
            free[(L, i, j)] = all(dist(x, y, o) >= W/2+CL for o in obs[L])
for i in range(nx):
    for j in range(ny):
        x, y = X(i), Y(j)
        via_ok[(i, j)] = (all(dist(x, y, o) >= VR+CL for L in (F, B) for o in obs[L]) and
                          all(math.hypot(x-tomm(h.x), y-tomm(h.y)) > 1.0 for h in holes) and not (x > 171.4 and 114.6 < y < 142.4))
# main pour: big filled outlines; stray group = outlines containing the source points
big = {}
for L in (F, B):
    ps = zl[L].GetFilledPolysList(L); big[L] = [ps.Outline(k) for k in range(ps.OutlineCount()) if ps.Outline(k).Area() > 100e12]
def in_main(L, x, y):
    p = pcbnew.VECTOR2I(mm(x), mm(y)); return any(o.PointInside(p) for o in big[L]) and zl[L].HitTestFilledArea(L, p)
start = []
for k in range(41):
    x, y = sx1+(sx2-sx1)*k/40, sy1+(sy2-sy1)*k/40
    i, j = round((x-RX0)/G), round((y-RY0)/G)
    for L in (F, B):
        if free.get((L, i, j)): start.append((L, i, j))
prev = {s: None for s in start}; q = collections.deque(start); goal = None
while q:
    c = q.popleft(); L, i, j = c
    if in_main(L, X(i), Y(j)) and prev[c] is not None: goal = c; break
    nb = [(L, i+di, j+dj) for di, dj in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1))]
    if via_ok.get((i, j)): nb.append((B if L == F else F, i, j))
    for n in nb:
        if n not in prev and free.get(n):
            prev[n] = c; q.append(n)
if not goal: print('no path'); sys.exit(1)
path = []; c = goal
while c: path.append(c); c = prev[c]
path.reverse(); print('path cells', len(path), 'layers', ''.join('F' if p[0] == F else 'B' for p in path))
g = b.FindNet('GND'); segs = 0
k = 0
while k < len(path)-1:
    a = path[k]
    if path[k+1][0] != a[0]:
        v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(mm(X(a[1])), mm(Y(a[2])))); v.SetWidth(mm(2*VR)); v.SetDrill(mm(0.3))
        v.SetLayerPair(F, B); v.SetNet(g); b.Add(v); k += 1; continue
    m = k+1
    d = (path[m][1]-a[1], path[m][2]-a[2])
    while m+1 < len(path) and path[m+1][0] == a[0] and (path[m+1][1]-path[m][1], path[m+1][2]-path[m][2]) == d: m += 1
    t = pcbnew.PCB_TRACK(b); t.SetStart(pcbnew.VECTOR2I(mm(X(a[1])), mm(Y(a[2])))); t.SetEnd(pcbnew.VECTOR2I(mm(X(path[m][1])), mm(Y(path[m][2]))))
    t.SetWidth(mm(W)); t.SetLayer(a[0]); t.SetNet(g); b.Add(t); segs += 1; k = m
print('segments', segs)
pcbnew.ZONE_FILLER(b).Fill([z for z in b.Zones() if not z.GetIsRuleArea()]); b.Save(out)
