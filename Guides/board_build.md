# Board Build Guide

This guide walks you through ordering, assembling, and smoke-testing the
QuailTracker V5 hardware. By the end of it you'll have a powered, blank
board ready to flash — picking up from there is the [bootstrap guide](bootstrap.md).

For the system-level "what is this and what does it do" view, see
[`docs/ecosystem.md`](../docs/ecosystem.md).

---

![Finished main board + two mic breakouts](images/overview.jpg)

## What you're building

Two PCBs:

- **Main board** (~50×60 mm) — STM32U575 MCU, ESP32-C3 companion radio,
  GPS, MicroSD slot, solar charger, all the connectors. Most of the SMD
  is JLCPCB-assembled; you hand-solder the through-hole connectors and
  the ESP32-C3 module.
- **Mic breakout** (~10×8 mm) — single IM72D128 PDM mic + decoupling
  cap + solder pads for the cable. You build **two** of these per
  station: one configured as Left, one as Right (selected via a solder
  jumper). They daisy-chain back to the main board on a short cable
  that you solder directly to the breakout pads.

A finished station also needs a battery, a small solar panel, an active
GPS antenna, an SHT30 module, and an enclosure — but those are off-board
items, not part of this guide.

## Tools and skills you'll need

- Fine-tip soldering iron (for through-hole connectors and the ESP32-C3
  castellated module)
- Hot plate or hot-air rework station (for the mic breakout — IM72D128
  has a sound port underneath, so an iron alone won't reflow it)
- Stencil for the mic breakout's solder paste
- Tweezers, fine
- Multimeter
- Microscope or a 10× loupe (highly recommended for inspecting the
  ESP32-C3 castellated joints and the IM72D128 pads)
- USB-C cable

You should be comfortable with through-hole soldering and basic SMD
inspection. JLCPCB does the fine-pitch SMD work, so you don't need to
hand-solder 0402 passives or the LQFP-100.

## Bill of materials

Three places you'll buy from:

- **JLCPCB / LCSC**, via the SMT-assembled board orders below — covers
  everything except the modules. The canonical lists are
  [`hardware/stm32u575_bom_lcsc.csv`](../hardware/stm32u575_bom_lcsc.csv)
  and [`hardware/mic_breakout_bom.csv`](../hardware/mic_breakout_bom.csv).
- **Amazon** — ESP32-C3 Super Mini module, SHT30 module, active GPS
  ceramic-patch antenna with U.FL pigtail, 5V/2W solar panel, and the
  2.54 mm pin header strips for the through-hole headers.
- **Battery** — 1S2P 18650 pack, ~6800 mAh. Buy individual cells from a
  reputable distributor (IMR Batteries, Liion Wholesale, 18650 Battery
  Store) — Samsung 30Q or 35E are good picks. Avoid generic Amazon
  listings for cells specifically; the protected-cell counterfeits are
  common and the capacity numbers are routinely lies.

## Order PCBs

Two separate JLCPCB orders. Settings for both: 2-layer, standard
tolerances, economic SMT assembly — JLC will assemble all the SMD
components from the LCSC BOM.

- **Main board**: upload [`hardware/QuailTracker_Main_v5.zip`](../hardware/QuailTracker_Main_v5.zip)
  (Gerbers + BOM + Pick-and-Place). Order it as-is — the board is large
  enough that the per-board cost is reasonable without panelizing.
- **Mic breakout**: upload [`hardware/QuailTracker_Mic_v2.zip`](../hardware/QuailTracker_Mic_v2.zip).
  At ~10×8 mm the per-board cost without panelizing is silly — fixed
  setup fees swamp the actual material cost. **Enable "Panel by JLCPCB"**
  in the order flow with these settings:
  - 5 columns × 5 rows
  - Rails on top and bottom (needed so the pick-and-place machine has
    something to grip during SMT assembly)

  That gives you 25 mic breakouts per panel, drops the per-mic cost to
  something sane, and the rails snap off cleanly with a V-groove or
  mouse-bite separation. Add this in the JLC order comments:

  > Reduce stencil apertures to 70% scale for this design.

  Without this, the IM72D128's pads end up with too much paste and bridge
  during reflow. We've confirmed with JLC support that they'll honor the
  request when it's in the order notes. Solder wick + reflow can rescue a
  bridged board, but it's much easier to avoid in the first place.

Quantity: one panel of mics (25 boards) is plenty for a few stations
plus rework spares — you need 2 per station, and you *will* lose one to
a short or a misaligned mic at some point. Five main boards is a
reasonable starting point.

## When the boards arrive

![Bare main board and mic breakout fresh from JLC](images/bare_boards.jpg)

Inspect each board under a microscope or loupe before doing anything else:

- **ESP32-C3 footprint on the main board** — the V5 pads extend
  1.5 mm past the module outline on each side, so the joints are easy
  to see and easy to rework. Just confirm a clean fillet on every
  castellated pad before moving on.
- **STM32U575** — visual scan of the LQFP-100 pins for bridges or
  missed pads. JLC's reject rate on QFP-100 is low but not zero.
- **Mic breakout** — bare PCB at this stage (you'll be assembling it
  yourself). Glance at it for obvious JLC defects, then move on. There
  isn't much to inspect on an unpopulated 10×8 mm board.

Tally the AliExpress + battery + header parts off the BOM and confirm
nothing's missing or substituted.

## Hand-solder the main board

JLCPCB's economic SMT service handles all the surface-mount parts —
STM32U575, GPS module, MicroSD slot, solar charger IC, passives, the
two TPS22916 load switches, and the RESET/BOOT0 tactile buttons (those
are SMD on V5). Everything below is on you.

- **ESP32-C3 Super Mini (U3)** — castellated module, hand-soldered.
  Pre-tin one pad, set the module flush against the edge marker, tack
  the pre-tinned pad while pressing the module flat against the PCB,
  then work around the remaining pads. Inspect each joint — you want a
  clear fillet from the castellated half-hole down to the PCB pad. The
  ceramic antenna sits over the antenna keep-out zone (no copper either
  layer); do not try to reflow under it.

  ![ESP32-C3 module on castellated pads, antenna over keep-out](images/esp32_placement.jpg)
- **JST PH through-hole connectors:**
  - **CN1** (battery, 2-pin) — locking notch faces outward (toward the
    cable).
  - **CN2** (mic breakout, 4-pin) — same.
  - **CN3** (solar panel, 2-pin) — same.
- **Pin headers** (1×40 strips from Amazon, snap to length — JLC charges
  an "extended part" fee for these and they're a few cents each
  in bulk, so it's not worth assembling them at the fab):
  - **H2** (debug, 1×7) — always fit it. SWD + SWO + serial console all
    on one header. You'll want J-Link or USB-UART access at some point
    for development or recovery, and the cost to add it now is zero.
  - **H1** (SHT30, 1×4) — your call: either solder a 1×4 male header
    and hang the SHT30 daughter board off it, or skip the header
    entirely and thread the SHT30's wires straight through the holes.
    Both work.
  - **P1** (laser wake, 1×4) — for the optional laser-wake circuit,
    which lets a deployed station be roused from deep sleep by aiming
    a laser pointer at it. The off-board parts are a **phototransistor**
    (mounted facing outward through the enclosure wall) and a **"hit"
    LED** that lights when the laser is on target — visual feedback so
    you know your aim is good without needing a response from the
    station first. The two on-board resistors (R19 pull-down, R20 LED
    current-limit) are already populated by JLC. Fit P1 only once you've
    decided where the phototransistor and hit LED will sit in the
    enclosure; the 2.54 mm pitch matches standard 4-pin Dupont cables.
- **U.FL pigtail to the GPS active antenna** — clip on when ready to
  deploy. The connector is delicate; don't repeatedly mate/unmate it
  during bring-up.

**Don't install the battery yet.** The ESP32-C3's USB-C port provides
plenty of power for bring-up via the always-on 3V3 rail.

## Build the mic breakouts

You'll do this twice per station — once for Left, once for Right.

1. Apply solder paste through the stencil onto the breakout's pads.
   With the 70% stencil aperture you should get a thin, well-defined
   deposit on each pad.
2. Place parts:
   - **MIC1** — IM72D128, mind the pin-1 indicator. The acoustic port
     is on the underside, aligned with the 0.8 mm sound hole through
     the PCB.
   - **C_MIC** — 100 nF decoupling cap.
   - The cable to the main board doesn't use a connector — it solders
     directly to the wire pads on the breakout (CLK, DATA, VDD, GND).
     There are two pad sets so you can daisy-chain a second breakout
     off the first when wiring stereo.
3. Reflow on a hot plate using the paste manufacturer's profile
   (typically: ramp to ~217 °C, peak ~235 °C, hold ~30 s, cool slowly).

   ![Mic breakout post-reflow](images/mic_breakout_reflow.jpg)
4. **Functional test is the only verification.** The IM72D128's pads
   are hidden underneath the chip, so you can't see them — visual
   inspection won't tell you whether the joints are good or whether
   adjacent pads bridged. The only way to know is to play the mic.

   Once the breakout is wired up and the [bootstrap guide](bootstrap.md)
   has put the mic test firmware on the board, tap each mic in turn
   and confirm the matching L or R bar moves on the RTT terminal. If a
   breakout is silent or behaves erratically: suspect a short or cold
   joint under the chip. Apply fresh flux around the perimeter, briefly
   re-reflow on the hot plate, and retest. The IM72D128 survives a
   reasonable number of reflow cycles.
5. **Solder the L/R jumper.**
   - The breakout has a 3-pad solder jumper. The center pad is the
     IM72D128's `SEL` input; the outer pads are tied to GND ("Left"
     silkscreen side) and VDD ("Right" silkscreen side).
   - For the Left mic, bridge the center pad to the **"Left"** side.
   - For the Right mic, bridge the center pad to the **"Right"** side.
   - The firmware is bench-validated against these silkscreen labels —
     trust the silkscreen, not any datasheet convention you might
     find online.

   ![L/R solder jumpers — Left bridged on one breakout, Right on the other](images/lr_jumper.jpg)

Each station needs one Left and one Right breakout, mounted with the
sound holes facing outward through the enclosure with whatever spacing
you've decided on for TDOA bearing estimation.

For the cable from the breakouts back to the main board's CN2 (4-pin
JST PH), shielded 4-conductor wire works well — guitar pickup wire is
a popular pick — 50–100 mm depending on enclosure layout. Crimp a JST
PH housing on the main-board end; solder the four conductors directly
to the wire pads on the first breakout, daisy-chain the second
breakout off the unused pad set on the first. Tie the shield drain to
GND on the main-board end only (single-end ground); leave it floating
on the breakout end.

![Cable wiring — JST PH on main-board end, soldered to first breakout, daisy-chained to second](images/mic_cable.jpg)

## First power-on smoke test

This confirms the rails are alive before you start flashing anything.

A note on power topology: the 3V3 rail has two upstream sources wired
together. With a battery in CN1, **Q1** (the LDO) regulates VBAT down
to 3V3. With USB-C plugged into the ESP32-C3, the ESP32 module's own
onboard regulator powers the rail through its 3V3 pin. Either source
alone is enough to bring the board up.

For the smoke test, use USB-C only — no battery yet:

1. Disconnect everything from the main board — no battery, no solar
   panel, no SD card, no mic cables, no GPS antenna.
2. Plug USB-C into the ESP32-C3 Super Mini.
3. Probe with the multimeter:
   - **3V3** rail at H2.1 (debug header pin 1) or any 3V3 decoupling
     cap: expect 3.30 V ± 0.05 V.
   - **VBAT+** at CN1 (no battery connected): floats near 0 V — no
     current path.

   ![Multimeter probe on H2.1 reading ~3.3 V](images/smoke_test_3v3.jpg)
4. The ESP32-C3's onboard LED should light up or blink depending on
   what factory firmware shipped on it.
5. The STM32U575 has no firmware yet, so it won't do anything visible.
   That's expected.

If the 3V3 rail is missing or wrong:

- Check that the ESP32-C3 itself is powered: its onboard 5V/VIN pin
  should sit near 5 V from USB, and its 3V3 output pin near 3.3 V. If
  *its* 3V3 is dead, the module's own regulator or solder joints to it
  are the problem.
- Check that the ESP32's 3V3 output is actually reaching the rest of
  the board through the U3.3V3 trace and the local decoupling cap (**C24**).
- Sanity-check for shorts between 3V3 and GND with the multimeter on
  continuity / resistance.

When you're ready to test battery-powered operation later, install the
battery in CN1 and verify Q1.VIN ≈ battery voltage and Q1.VOUT ≈ 3.3 V.

If the rails are clean, you're done with hardware. The board is ready
to flash.

## Next steps

Continue to the [**bootstrap guide**](bootstrap.md) to load firmware
onto the ESP32 and the STM32 — no J-Link required.
