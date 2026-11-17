import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
for f in b.GetFootprints():
    for p in f.Pads():
        if (f.GetReference(), p.GetNumber()) in (('JP2','3'),('JP3','3'),('J8','30'),('J8','29'),('J8','3'),('J8','4'),('J3','1'),('Q6','3'),('Q7','3'),('J7','6')):
            p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
pcbnew.ZONE_FILLER(b).Fill([z for z in b.Zones() if not z.GetIsRuleArea()])
b.Save(sys.argv[1])
