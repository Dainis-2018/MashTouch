# Silkscreen: hide footprint refs on silk (refs stay on Fab), add pin labels. usage: silk.py board
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
SILK = b.GetLayerID('F.SilkS')
# remove labels from a previous run
for d in list(b.GetDrawings()):
    if isinstance(d, pcbnew.PCB_TEXT) and d.GetLayer()==SILK : b.Delete(d)
for f in b.GetFootprints():
    if f.GetReference() != 'J8': f.Reference().SetVisible(False)
def T(text, x, y, rot=0, h=0.8, just=0):
    t = pcbnew.PCB_TEXT(b); t.SetText(text); t.SetLayer(SILK)
    t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(h), pcbnew.FromMM(h))); t.SetTextThickness(pcbnew.FromMM(0.15))
    t.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))); t.SetTextAngleDegrees(rot)
    if just<0: t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    b.Add(t)
# J1 OUT1-4, J2 PWR1-2, J3 12 V in: below the plug, one label per position
for x, s in zip([118.75,122.25,125.75,129.25,132.75,136.25,139.75,143.25], ['1+','1-','2+','2-','3+','3-','4+','4-']): T(s, x, 112.05)
for x, s in zip([150.25,153.75,157.25,160.75], ['P1+','P1-','P2+','P2-']): T(s, x, 112.05)
T('+12V', 167.6, 112.05); T('GND', 171.4, 112.05)
# J4 RTD + 1-Wire (left edge), reading upwards
for i, s in enumerate(['1F+','1R+','1R-','1F-','2F+','2R+','2R-','2F-','3V3','DQ','GND']): T(s, 112.1, 110.9+3.5*i, rot=90)
# J5 level (right tab)
T('IN', 178.0, 143.6, rot=90); T('GND', 178.0, 147.1, rot=90)
# J7 buttons (pin 1 at the bottom)
for i, s in enumerate(['START','PUMP','LED S','LED P','+5V','GND']): T(s, 174.3, 156.0-2.0*i+(0.4 if i==5 else 0), just=-1)
# JP1: OUT +V from 12 V (pins 1-2) or 5 V (pins 2-3)
T('12V', 124.3, 126.3); T('5V', 129.38, 126.3)
# JP2/JP3: 1-2 = 3-wire, 2-3 = 2- or 4-wire
for y in (121.1, 133.62): T('3W', 115.47, y); T('2/4W', 118.25, y)
# test points
for x, s in [(125.6,'5V'),(128.8,'GND'),(143.6,'IO45'),(146.8,'IO46'),(150.0,'BOOT'),(153.2,'RST')]: T(s, x, 155.4)
T('12V', 148.5, 135.9); T('3V3', 115.2, 150.4, just=-1)
b.Save(sys.argv[1])
