# Backpack routing scripts

How `hardware/backpack/backpack.kicad_pcb` was placed and routed on 2026‑11‑17. The board file is the source of truth now: **edit it in KiCad**. These scripts are kept only so a full re-route can be repeated after a big change. Several of them **delete or overwrite tracks**; run them on a copy.

All `.py` files run with KiCad's own Python (`C:\Program Files\KiCad\10.0\bin\python.exe`, which has `pcbnew`). Freerouting 2.5.0 (GPL‑3, `freerouting-2.5.0.jar` from its GitHub releases) needs Java 21 or newer.

| Step | Script | What it does |
|---|---|---|
| 1 | `place.py in out` | positions and rotations of every unlocked footprint; reports courtyard overlaps |
| 2 | `silk.py board` | hides silkscreen references, adds the terminal/jumper/test-point labels |
| 3 | `hand.py board` | **deletes all tracks, vias and zones**, then adds the hand-routed, locked copper: 12 V input and trunk, PWR1/PWR2 3 A paths, MOSFET source vias, buck switching loop and 5 V output, both RTD front ends, level input; plus the antenna-notch and board-edge keepouts |
| 4 | `fanout.py`, `fanfix.py` | a locked via (or short link) for every SMD GND pad, so the autorouter can leave GND out |
| 5 | `autoroute.sh in out tag` | exports Specctra DSN, removes GND from it (`strip_gnd.py`), runs Freerouting, imports the session (`imp.py`, which keeps the GND fan-out) and runs DRC. Needs `WORK` set to a scratch folder holding `fr/freerouting-2.5.0.jar` |
| – | `pipeline.sh tag` | steps 1, 3, 4 and 5 on the real board |
| 6 | `connect.py board net x1 y1 x2 y2 out` | finds a clear straight link for a connection the autorouter left open (used once: U1 FB to 5 V) |
| 7 | `widen.py`, `badtracks.py` | widen Freerouting's 0.10–0.19 mm neck-downs to 0.2 mm, reverting any that break clearance (loop until stable) |
| 8 | `zones.py`, `finish.py`, `solid.py`, `islands.py` | GND pour on both layers, drop one-sided vias, 2.5 mm stitching grid, solid connection for JP2/JP3/J8 GND pins, join leftover pour islands |
| – | `lee.py`, `join_pour.py` | small grid maze router and straight-link search, to join a stray pour island to the main ground (diagnosis aids) |
| 9 | `trim.py`, then `islands.py` | remove the free-standing stitching vias again (keeps pad fan-out and the buck area), then add back only the vias needed to join pour islands. 310 → 75 GND vias |

Freerouting is deterministic: the same input gives the same result, so improve the input (placement, hand routes) rather than rerunning.
