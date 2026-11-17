# I/O backpack (KiCad 10)

The MCU-less board that plugs into J8 of the Waveshare ESP32‑S3‑Touch‑LCD‑3.5. What it does and why: `docs/design.md` (*I/O backpack PCB*). Mechanics, zones and connector order: `docs/backpack-layout.md`.

| File | Contents |
|---|---|
| `backpack.kicad_pro` | the project: open this in KiCad |
| `backpack.kicad_sch` | the whole schematic on one A3 sheet, **wired**: J8 in the middle, outputs (OUT1–4, PWR1–2, JP1) on the left, the 12 V input and buck on top, PT100 (2× MAX31865, JP2/JP3, J4), 1‑Wire, level and the front panel (TCA9535, J7) on the right, decoupling, power flags and holes at the bottom. Power nets are symbols; every other net has one label with its name |
| `backpack.kicad_sym`, `sym-lib-table` | project symbols: MOSFETs, the ICs, J8 and the terminals, with the pins where the sheet needs them |
| `backpack.kicad_pcb` | outline with the antenna notch, holes, J8 and terminals at their measured positions; all parts placed (zones and reasons in `docs/backpack-layout.md`), silkscreen labels done, **routed** (two layers, GND pour on both) |
| `backpack.pretty/`, `fp-lib-table` | project footprints: the J8 header for the back side |
| `netlist-reference.txt` | the reviewed netlist, pin by pin, for `tools/kicad/netcheck.js` |
| `bom.csv` | grouped BOM with MPN, LCSC number where checked, and notes |
| `../../docs/img/backpack-schematic.svg` | the sheet exported as SVG, for reading without KiCad |

## State (2026‑11‑17)

- **Schematic:** complete first version, wired on one sheet, ERC clean. Converted from the label-per-pin sheet on 2026‑11‑17: same 358 pins on the same nets (only the former global names gained a `/`), same symbol UUIDs and fields, so the PCB stayed linked.
- **PCB:** placed, labelled and **routed** (2026‑11‑17). Placement: buck next to J8 pin 2, TCA9535 next to the I2C pins, test points and J9 below J8. Silkscreen names every terminal position and jumper setting; references are on the Fab layer only. Routing: power, 3 A paths, the buck loop, both RTD front ends and the level input by hand (locked tracks), the rest by Freerouting, then GND pours on both layers. 78 GND vias: one at every SMD ground pad, a few in the buck area and only the stitching vias needed to join the pour islands (the 2.5 mm stitching grid was removed again). Net classes (width / clearance): Power 0.8 / 0.25 mm (+12V, +5V, OUT_V, buck switch node, PWR outputs), Drive 0.5 / 0.25 mm (OUT/PWR loads and gates, LED ring drivers), Logic3V3 0.3 / 0.25 mm, Panel 0.25 / 0.2 mm (buttons, LED rings, I2C around the TCA9535, which need the tighter spacing), default (incl. GND) 0.25 / 0.25 mm; GND pour 0.3 mm from other nets. Current paths: 12 V input 3 mm, 12 V trunk 3 mm (2 mm after the PWR2 branch), PTC → terminal 2 mm, MOSFET tab → terminal 2 mm, three ground vias per MOSFET source; J3 GND, Q6/Q7 sources and J7/J8/JP2/JP3 ground pins connect solidly to the pour. **DRC: 0 violations, 0 unconnected, 0 schematic parity issues.** Not yet reviewed by a person: look it over in KiCad before ordering.
- **Parts:** LCSC numbers were checked for the four ICs (AP63205WU‑7 C2071056, MAX31865ATP+ C404011, UCC27511ADBVR C2676982, TCA9535PWR C130204). Match the passives and discretes to JLCPCB basic parts when ordering. Check stock at order time.
- **Hand-soldered:** terminals (J1–J5), J7, J8, J9, JP1–JP3, the fuse holder F1.

## Things to know

- **Wires connect wherever a pin end touches them.** Dropping a part onto a passing wire joins it to that net without any warning from ERC. After every schematic edit, run `tools/kicad/netcheck.js` (below) and read what it lists.
- **J8** is on the back layer (B.Cu) and uses the project footprint `backpack:PinHeader_2x16_P2.54mm_Vertical_Bottom` from `backpack.pretty/` (listed in the project `fp-lib-table`): the standard 2×16 header mirrored, because flipping the standard one would swap the odd/even rows. Keep the footprint locked. See `docs/backpack-layout.md`, *Orientation*.
- **J7** is a top-entry JST PH (B6B‑PH‑K): the plug goes in from the box side.
- The terminals are numbered so that their labels read left to right (top edge) and top to bottom (side edges) with the plug openings facing out.
- **PWR_FLAGs** (bottom of the sheet) tell ERC that +12V, +5V, +3V3 and GND are supplied (J3, the buck and the panel).
- **Library tables:** on a fresh KiCad install, open KiCad once so it creates its global symbol and footprint library tables. Until then ERC and DRC also list "library not in the configuration" warnings.
- **Generators:** `tools/kicad/backpack_sch.js` and `backpack_pcb.py` made the first versions; `backpack_overview.js` (with `backpack_draw.js`, `backpack_kicad.js`) made the wired sheet. They all overwrite their files: **edit in KiCad** and don't rerun them. The placement and routing scripts are in `tools/kicad/route/` (README there); they also overwrite tracks.

## Checks

After a schematic edit:

```
node ../../tools/kicad/netcheck.js            # lists every pin that changed net; exit 1 if any
node ../../tools/kicad/netcheck.js --update   # once all listed changes are intended
```

Then Update PCB from Schematic (F8) and:

```
kicad-cli sch erc --severity-all backpack.kicad_sch
kicad-cli pcb drc --severity-all --schematic-parity backpack.kicad_pcb
kicad-cli sch export svg -o . backpack.kicad_sch    # then move backpack.svg to ../../docs/img/backpack-schematic.svg
```
