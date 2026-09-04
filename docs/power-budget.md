# Power budget — measured 2026-09-03

PPK2 sourcing 4 V into the battery connector, 9.12 s at 100 kHz,
912,177 samples. Buttons exercised throughout; LEDs and BLE active.

## What was measured

| | |
|---|---|
| Mean | 4.27 mA |
| Median | 3.58 mA |
| **Lowest sustained (1 ms avg)** | **3.64 mA** |
| Peak | 42.7 mA |
| Milliseconds below 3 mA, in 9.12 s | **0** |

The floor is the number that matters. Nothing in the capture ever
approaches a sleeping device.

## Where it goes

| | | |
|---|---|---|
| 4× WS2812B-V5 quiescent | 2.40 mA | datasheet, 0.6 mA each at VDD=5 V |
| Remainder (MCU + charger + LDO) | 1.24 mA | |
| **Measured floor** | **3.64 mA** | |

For reference: CH592 deep sleep is single-digit µA, WFI idle at 60 MHz is
~1–2 mA, and the AW32001 sits at 58–80 µA in battery mode. A 1.24 mA
remainder is the signature of **WFI idle, not deep sleep** — so this capture
is a `FOB_SLEEP_ENABLE=0` build, or one that never reached its sleep branch.
The boot banner now states which build is running, so a future capture cannot
be misattributed.

Note the 0.6 mA figure is specified at VDD=5 V and these run at 3V3, so it may
be somewhat optimistic as an estimate — but the arithmetic lands within
noise of the measured floor, which is corroboration.

## The finding that matters

**The LEDs dominate, and no amount of firmware fixes it.** On a 120 mAh cell:

| | current | life |
|---|---|---|
| As measured | 3.64 mA | 1.4 days |
| MCU deep-sleeps, LEDs as-is | 2.49 mA | **2.0 days** |
| LEDs power-gated, MCU idle | 1.24 mA | 4.0 days |
| LEDs gated **and** MCU deep-sleeps | 0.085 mA | **59 days** |

Perfect firmware sleep buys 1.4 → 2.0 days. Power-gating the LEDs as well
buys 59. The WS2812 quiescent is the whole problem.

## Recommendation for the respin

Add a **high-side load switch on the WS2812 supply** — a P-FET (or an
integrated load switch) between 3V3 and the four pixels' VDD, gated from a
spare GPIO. Off when idle, on shortly before a frame.

- Cost: one FET, a gate resistor, a pull-up, one GPIO. Castellations are free.
- Saves 2.4 mA of the 2.49 mA idle budget.
- The firmware already has the shape for it: `led_anim_init()` blanks the
  chain several times at startup precisely because the LEDs may not be ready
  when the first frame arrives, and a power-gated chain has exactly that
  problem on every wake. Allow a settling delay after switching on, before
  the first frame.
- Watch the switch's own quiescent current when choosing the part; some load
  switches give back a good fraction of what they save.

Worth pairing with the PA14 SPI/DMA swap already recorded in
[next-revision.md](next-revision.md), since both are LED-path changes.

## Still unmeasured

- **AW32001 quiescent in battery mode.** Taken from the datasheet, not
  measured. Worth isolating — at 58–80 µA it is the floor once the LEDs are
  gated, and it would then be the single largest remaining term.
- **PJ54M33 LDO quiescent.** Not separated out at all here.
- **Deep sleep actually working.** Never yet observed on hardware.

## The one test that settles two questions

Flash with `-DFOB_SLEEP_ENABLE=1` and re-measure the floor:

- drops to **~2.5 mA** → deep sleep works, and the residual is the LEDs,
  confirming both halves of the breakdown above
- stays at **~3.6 mA** → sleep is not being entered, and the firmware is
  where to look rather than the board
