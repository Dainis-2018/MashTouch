# zones.py board: GND pour on both layers (inside the edge and notch keepouts), then fill
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.FromMM
for z in list(b.Zones()):
    if not z.GetIsRuleArea(): b.Delete(z)
for layer in (pcbnew.B_Cu, pcbnew.F_Cu):
    z = pcbnew.ZONE(b); z.SetLayer(layer); z.SetNet(b.FindNet('GND'))
    z.SetLocalClearance(mm(0.3)); z.SetMinThickness(mm(0.25)); z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.5)); z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    o = z.Outline(); o.NewOutline()
    for x,y in ((100,100),(190,100),(190,159),(100,159)): o.Append(mm(x),mm(y))
    b.Add(z)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(sys.argv[1])
