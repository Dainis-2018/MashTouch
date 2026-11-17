# MashTouch - creates the first version of hardware/backpack/backpack.kicad_pcb
# from the schematic netlist, with the outline, holes, J8 and the terminals at
# the positions in docs/backpack-layout.md, and every other part packed into
# its zone. Bootstrap only: once the board is edited in KiCad, do not rerun.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   kicad-cli sch export netlist --format kicadsexpr -o backpack.net backpack.kicad_sch
#   "C:/Program Files/KiCad/10.0/bin/python.exe" tools/kicad/backpack_pcb.py hardware/backpack
import os
import re
import sys

import pcbnew

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..', 'hardware', 'backpack')
FP_DIR = 'C:/Program Files/KiCad/10.0/share/kicad/footprints'
OX, OY = 100.0, 100.0  # board origin on the KiCad page (layout x/y are added)
W, H = 90.0, 59.0
NOTCH = (74.0, 17.0, 40.0)  # x, y0, y1: no board over the panel's chip antenna


# ---- netlist -----------------------------------------------------------------
def parse(text):
    tokens = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text)
    pos = 0

    def item():
        nonlocal pos
        t = tokens[pos]
        pos += 1
        if t == '(':
            lst = []
            while tokens[pos] != ')':
                lst.append(item())
            pos += 1
            return lst
        if t.startswith('"'):
            return t[1:-1].replace('\\"', '"').replace('\\\\', '\\')
        return t
    return item()


def find(lst, key):
    for e in lst:
        if isinstance(e, list) and e and e[0] == key:
            return e
    return None


def find_all(lst, key):
    return [e for e in lst if isinstance(e, list) and e and e[0] == key]


net = parse(open(os.path.join(OUT, 'backpack.net'), encoding='utf-8').read())
comps = []
for c in find_all(find(net, 'components'), 'comp'):
    sp = find(c, 'sheetpath')
    comps.append({
        'ref': find(c, 'ref')[1], 'value': find(c, 'value')[1], 'fp': find(c, 'footprint')[1],
        'sheet': find(sp, 'names')[1], 'path': find(sp, 'tstamps')[1] + find(c, 'tstamps')[1],
    })
pad_net = {}
for n in find_all(find(net, 'nets'), 'net'):
    name = find(n, 'name')[1]
    for node in find_all(n, 'node'):
        pad_net[(find(node, 'ref')[1], find(node, 'pin')[1])] = name

# ---- board ---------------------------------------------------------------------
board = pcbnew.BOARD()
mm = pcbnew.FromMM


def V(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))


nets = {}
for name in sorted(set(pad_net.values())):
    ni = pcbnew.NETINFO_ITEM(board, name)
    board.Add(ni)
    nets[name] = ni


def edge_line(a, b):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(mm(0.1))
    s.SetStart(V(*a))
    s.SetEnd(V(*b))
    board.Add(s)


def edge_arc(start, mid, end):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(mm(0.1))
    s.SetArcGeometry(V(*start), V(*mid), V(*end))
    board.Add(s)


def corner(cx, cy, r, a0):
    """Quarter arc around (cx, cy) from angle a0 to a0 + 90 (degrees, screen y down)."""
    import math
    pts = []
    for a in (a0, a0 + 45, a0 + 90):
        t = math.radians(a)
        pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    edge_arc(*pts)


R, Q = 5.0, 1.0
nx, ny0, ny1 = NOTCH
# clockwise from the top-left: top edge, top-right corner, right edge down to the notch
edge_line((R, 0), (W - R, 0))
corner(W - R, R, R, 270)
edge_line((W, R), (W, ny0 - Q))
corner(W - Q, ny0 - Q, Q, 0)
edge_line((W - Q, ny0), (nx + Q, ny0))
corner(nx + Q, ny0 + Q, Q, 180)  # inner corner, notch top
edge_line((nx, ny0 + Q), (nx, ny1 - Q))
corner(nx + Q, ny1 - Q, Q, 90)
edge_line((nx + Q, ny1), (W - Q, ny1))
corner(W - Q, ny1 + Q, Q, 270)
edge_line((W, ny1 + Q), (W, H - R))
corner(W - R, H - R, R, 0)
edge_line((W - R, H), (R, H))
corner(R, H - R, R, 90)
edge_line((0, H - R), (0, R))
corner(R, R, R, 180)


# ---- footprints -------------------------------------------------------------------
def load(c):
    lib, name = c['fp'].split(':')
    fp = pcbnew.FootprintLoad(os.path.join(FP_DIR, lib + '.pretty'), name)
    if fp is None:
        raise SystemExit('footprint not found: ' + c['fp'])
    fp.SetFPIDAsString(c['fp'])
    fp.SetReference(c['ref'])
    fp.SetValue(c['value'])
    fp.SetPath(pcbnew.KIID_PATH(c['path']))
    fp.SetSheetname(c['sheet'].strip('/').split('/')[-1] if c['sheet'] != '/' else '')
    board.Add(fp)
    for pad in fp.Pads():
        n = pad_net.get((c['ref'], pad.GetNumber()))
        if n:
            pad.SetNet(nets[n])
    return fp


def bbox(fp):
    """Courtyard box (layout mm): x0, y0, x1, y1."""
    b = fp.GetCourtyard(pcbnew.F_CrtYd if not fp.IsFlipped() else pcbnew.B_CrtYd).BBox()
    if b.GetWidth() == 0:  # no courtyard (test points): the pads plus a margin
        b = None
        for p in fp.Pads():
            pb = p.GetBoundingBox()
            if b is None:
                b = pb
            else:
                b.Merge(pb)
        b.Inflate(mm(0.5))
    return (pcbnew.ToMM(b.GetLeft()) - OX, pcbnew.ToMM(b.GetTop()) - OY,
            pcbnew.ToMM(b.GetRight()) - OX, pcbnew.ToMM(b.GetBottom()) - OY)


def pad_xy(fp, num):
    for p in fp.Pads():
        if p.GetNumber() == num:
            return pcbnew.ToMM(p.GetPosition().x) - OX, pcbnew.ToMM(p.GetPosition().y) - OY
    raise KeyError(num)


def move_by(fp, dx, dy):
    fp.Move(pcbnew.VECTOR2I(mm(dx), mm(dy)))


fps = {c['ref']: (c, load(c)) for c in comps}

# J8: pad 1 at (25.95, 53.75), pad 2 above it (inner row), pad 3 to the right.
# The header body sits on the back (facing the panel's socket), so J8 uses
# backpack:PinHeader_2x16_P2.54mm_Vertical_Bottom, the standard footprint
# mirrored left to right: flipped to B.Cu, its pad numbers land on the right
# socket pins again. Only one orientation puts every pad over its pin.
j8 = fps['J8'][1]
j8.Flip(j8.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
for deg in (0, 90, 180, 270):
    j8.SetOrientationDegrees(deg)
    x1, y1 = pad_xy(j8, '1')
    x2, y2 = pad_xy(j8, '2')
    x3, y3 = pad_xy(j8, '3')
    if abs(x2 - x1) < 0.01 and y2 - y1 < -2 and x3 - x1 > 2 and abs(y3 - y1) < 0.01:
        break
else:
    raise SystemExit('no J8 orientation matches')
x1, y1 = pad_xy(j8, '1')
move_by(j8, 25.95 - x1, 53.75 - y1)
j8.SetLocked(True)

for i, (x, y) in enumerate([(9, 5.25), (81, 5.25), (9, 53.75), (81, 53.75)]):
    h = fps['H%d' % (i + 1)][1]
    h.SetPosition(V(x, y))
    h.SetLocked(True)


order = {}


def place_row(ref, first_pin_xy, along, outward):
    """Terminal block: pads in a row from first_pin_xy along 'along' (+x or +y),
    the plug opening facing 'outward' ('up', 'left', 'right', 'down')."""
    fp = fps[ref][1]
    for deg in (0, 90, 180, 270):
        fp.SetOrientationDegrees(deg)
        fp.SetPosition(V(0, 0))
        a = pad_xy(fp, '1')
        b = pad_xy(fp, '2')
        d = (b[0] - a[0], b[1] - a[1])
        if (along == 'x' and abs(d[0]) > 1 and abs(d[1]) < .01) or (along == 'y' and abs(d[1]) > 1 and abs(d[0]) < .01):
            x0, y0, x1, y1 = bbox(fp)
            ext = {'up': a[1] - y0, 'down': y1 - a[1]} if along == 'x' else {'left': a[0] - x0, 'right': x1 - a[0]}
            # the opening is on the side where the housing reaches furthest from the pins
            if max(ext, key=ext.get) == outward:
                break
    else:
        raise SystemExit('no orientation for ' + ref)
    # first_pin_xy is the slot of the lowest x (or y) pad, whichever pin that is
    pads = sorted((pad_xy(fp, p.GetNumber()) for p in fp.Pads()), key=lambda q: q[0] if along == 'x' else q[1])
    move_by(fp, first_pin_xy[0] - pads[0][0], first_pin_xy[1] - pads[0][1])
    a = pad_xy(fp, '1')
    order[ref] = 'pin 1 at %s' % (('left' if a[0] <= pads[0][0] + .01 else 'right') if along == 'x' else ('top' if a[1] <= pads[0][1] + .01 else 'bottom'))
    # pull the housing flush with the board edge
    x0, y0, x1, y1 = bbox(fp)
    if outward == 'up':
        move_by(fp, 0, 0.3 - y0)
    elif outward == 'left':
        move_by(fp, 0.3 - x0, 0)
    elif outward == 'right':
        move_by(fp, (W - 0.3) - x1, 0)
    fp.SetLocked(True)


T = 3.5
x0 = 45 - 16 * T / 2 + T / 2        # 16 slots: 8 + gap + 4 + gap + 2
place_row('J1', (x0, 4), 'x', 'up')                # OUT1-4
place_row('J2', (x0 + 9 * T, 4), 'x', 'up')        # PWR1-2
place_row('J3', (x0 + 14 * T, 4), 'x', 'up')       # 12 V in
place_row('J4', (4, 10.9), 'y', 'left')            # RTD1, RTD2, 1-Wire
place_row('J5', (W - 4, 43.6), 'y', 'right')       # level


# ---- zones (layout mm: x0, y0, x1, y1) ----------------------------------------------
# Zones from docs/backpack-layout.md, resized to the measured part areas.
ZONES = {
    'E': (9.5, 8.5, 21.5, 39.5),   # RTD
    'D': (21.5, 9.5, 44.0, 34.0),  # OUT1-4
    'C': (44.0, 9.5, 58.5, 34.0),  # PWR1-2
    'B': (58.5, 9.5, 73.5, 41.0),  # 12 V in + buck
    'F': (9.5, 39.5, 24.0, 46.0),  # 1-Wire
    'G': (56.0, 41.0, 81.0, 49.6), # front panel, level
    'X': (21.5, 34.0, 56.0, 49.6), # J8 fan-out: decoupling, test points
}
fixed = {'J1', 'J2', 'J3', 'J4', 'J5', 'J8', 'H1', 'H2', 'H3', 'H4'}
first = [('J7', (56.0, 41.0, 79.0, 58.5)), ('J9', (56.0, 41.0, 79.0, 58.5))]  # placed before the rest
short = lambda n: n.split('/')[-1]


def zone_of(c):
    s, ref = c['sheet'], c['ref']
    nets_of = {short(pad_net.get((ref, p.GetNumber()), '')) for p in fps[ref][1].Pads()}
    if s.startswith('/12 V'):
        return 'B'
    if s.startswith('/Outputs'):
        if ref == 'JP1':
            return 'D'
        return 'C' if any(n.startswith('PWR') for n in nets_of) or nets_of == {'+12V', 'GND'} else 'D'
    if s.startswith('/PT100'):
        return 'E'
    if s.startswith('/1-Wire'):
        return 'F' if any(n.startswith('OW_') or n == 'ONEWIRE' for n in nets_of) else 'G'
    return 'X'


# obstacles: fixed parts, hole keepouts, the antenna notch (layout mm boxes)
GAP = 0.25
placed = []
for ref in fixed:
    placed.append(bbox(fps[ref][1]))
for (hx, hy) in [(9, 5.25), (81, 5.25), (9, 53.75), (81, 53.75)]:
    placed.append((hx - 2.5, hy - 2.5, hx + 2.5, hy + 2.5))
placed.append((NOTCH[0] - 2.0, NOTCH[1] - 1.0, W, NOTCH[2] + 1.0))  # notch + 2 mm keepback


def free(b):
    if b[0] < 0.5 or b[1] < 0.5 or b[2] > W - 0.5 or b[3] > H - 0.5:
        return False
    for o in placed:
        if b[0] < o[2] + GAP and b[2] + GAP > o[0] and b[1] < o[3] + GAP and b[3] + GAP > o[1]:
            return False
    return True


def put(fp, area):
    fp.SetPosition(V(0, 0))
    bx0, by0, bx1, by1 = bbox(fp)
    w, h = bx1 - bx0, by1 - by0
    zx0, zy0, zx1, zy1 = area
    y = zy0
    while y + h <= zy1 + 1e-6:
        x = zx0
        while x + w <= zx1 + 1e-6:
            b = (x, y, x + w, y + h)
            if free(b):
                move_by(fp, x - bx0, y - by0)
                placed.append(b)
                return True
            x += 0.5
        y += 0.5
    return False


def put_turned(fp, area):
    for deg in (0, 90, 180, 270):
        fp.SetOrientationDegrees(deg)
        if put(fp, area):
            return True
    return False


for ref, area in first:
    if not put_turned(fps[ref][1], area) and not put_turned(fps[ref][1], (3, 3, W - 3, H - 3)):
        print('could not place', ref)

groups = {}
for ref, (c, fp) in fps.items():
    if ref not in fixed and ref not in dict(first):
        groups.setdefault(zone_of(c), []).append(fp)

area_of = lambda f: (lambda b: (b[2] - b[0]) * (b[3] - b[1]))(bbox(f))
overflow = []
for z in ['B', 'C', 'D', 'E', 'G', 'F', 'X']:
    for fp in sorted(groups.get(z, []), key=lambda f: -area_of(f)):
        if put(fp, ZONES[z]):
            continue
        if put(fp, ZONES['X']) or put(fp, (3, 3, W - 3, H - 3)):
            overflow.append('%s (%s)' % (fp.GetReference(), z))
        else:
            overflow.append('%s (%s, NOT PLACED)' % (fp.GetReference(), z))

# design rules: JLCPCB-friendly defaults
ds = board.GetDesignSettings()
ds.m_TrackMinWidth = mm(0.15)
ds.m_MinClearance = mm(0.15)
ds.m_ViasMinSize = mm(0.45)
ds.m_MinThroughDrill = mm(0.3)

path = os.path.join(OUT, 'backpack.kicad_pcb')
pcbnew.SaveBoard(path, board)
print('saved', path, '-', len(fps), 'footprints')
if overflow:
    print('outside their zone (move by hand):', ', '.join(overflow))
