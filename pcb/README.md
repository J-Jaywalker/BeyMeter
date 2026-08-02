# BeyBeetle carrier board

KiCad 9 project for the board everything plugs into. It's a motherboard rather
than a proper PCBA — every module is an Adafruit breakout sitting in a
through-hole female header, so nothing is soldered down permanently and any part
can be pulled and replaced.

Board as ordered: 80mm x 60mm, 1.6mm FR4, lead-free HASL, 1oz copper, all
through-hole at 2.54mm pitch, ground plane on B.Cu. First run went to JLCPCB on
2026-06-19.

## What's in here

| Path | What it is |
|------|------------|
| `BeyBeetle.kicad_pro` | Project file — open this one |
| `BeyBeetle.kicad_sch` | Schematic |
| `BeyBeetle.kicad_pcb` | Board layout |

The `.kicad_prl`, `fp-info-cache` and `BeyBeetle-backups/` are all gitignored —
KiCad regenerates them and they're either huge or specific to whoever's machine
the project was last opened on.

## Getting boards made

No gerbers here — roll your own from the files above so you know they match the
board you're looking at, and so you can tweak it first if you want to. In KiCad:
File → Fabrication Outputs → Gerbers, then Drill Files, then zip the folder and
upload that. Most fabs are happy with KiCad's defaults. Mine came from JLCPCB
with the spec at the top of this page.

## Connectors

Every connection is documented in the schematic, but the short version:

| Ref | Module |
|-----|--------|
| J1, J2 | Feather RP2040 (16-pin left, 12-pin right) |
| J3 | MAX17048 fuel gauge |
| J4 | ISM330DHCX IMU (I2C side) |
| J5, J6 | EyeSPI breakout (top and bottom rows) |
| J7 | D2F lock switch |
| J8 | TCRT5000 IR sensor |

One thing to fix on the next revision: EyeSPI `LITE` is a no-connect, so the
display backlight is permanently on and the firmware's sleep mode can't switch
it off. It wants routing to a spare GPIO — 27 (A1) is free.
