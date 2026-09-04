# Power budget — measured 2026-09-03

PPK2 sourcing 4 V into the battery connector, 9.12 s at 100 kHz,
912,177 samples. Buttons exercised throughout; LEDs and BLE active.

> **Correction.** An earlier version of this document costed the LEDs from
> the **WS2812B-V5** datasheet at 0.6 mA quiescent each, concluded they were
> 2.4 mA of a 2.49 mA idle budget, and recommended a load switch for the
> respin. The parts fitted are **V6**, whose quiescent is **≤1 µA** — four of
> them are 4 µA, not 2.4 mA. The conclusion was wrong by three orders of
> magnitude and inverted: the LEDs are negligible, the MCU is the entire
> budget, and no board change is needed for this. The load-switch
> recommendation is withdrawn.

## What was measured

| | |
|---|---|
| Mean | 4.27 mA |
| Median | 3.58 mA |
| **Lowest sustained (1 ms avg)** | **3.64 mA** |
| Peak | 42.7 mA |
| Milliseconds below 3 mA, in 9.12 s | **0** |

## Where it goes

| | | |
|---|---|---|
| 4× WS2812B-V6 quiescent | 0.004 mA | ≤1 µA each, datasheet |
| AW32001 quiescent | 0.080 mA | datasheet, **unverified** |
| LDO quiescent | ~0.010 mA | rough |
| **Unaccounted — the MCU** | **3.548 mA** | **97% of the budget** |

3.5 mA is a CH592 idling in WFI at 60 MHz with the PLL up. Deep sleep is
single-digit µA. So the device is not sleeping, and essentially the entire
consumption is one thing.

## What that's worth

On the 120 mAh cell:

| | current | life |
|---|---|---|
| As measured | 3.64 mA | **1.4 days** |
| MCU deep sleep (5 µA) + everything else | 0.099 mA | **~50 days** |

Getting deep sleep working is worth roughly 35× — and it is entirely
firmware. Nothing on the board needs to change for it.

## Next step

Flash with sleep enabled and re-measure:

```bash
make clean && make EXTRA_CFLAGS="-ffreestanding -Iinclude -DFOB_SLEEP_ENABLE=1" && make flash
```

The boot banner now states which build is running, so a capture cannot be
misattributed. Expect the floor to fall from 3.64 mA to under 0.2 mA. If it
does not move, the sleep branch is not being reached and `lefob.c` is where
to look, not the board.

## Still unmeasured

Once the MCU sleeps, the **AW32001 becomes the largest remaining term** by a
wide margin — 80 µA against the MCU's 5 µA. That figure is from the datasheet
and has never been checked on this board. It is worth isolating before
assuming the ~50 day number, because it is the one component that could
halve it.

## Note on the earlier LED figure

The V5 and V6 parts share identical *timing* windows (T0H 220–380 ns, the
rest 580 ns–1 µs, RES >280 µs), which is why the driver work done against the
V5 datasheet is still correct. They differ sharply in quiescent current, which
is what the "improved sleep characteristics" of these parts refers to. Reading
the wrong one gave the right timings and a badly wrong power number.

**V6 does add one constraint V5 does not state:** `data cycle T0H+T0L,
T1H+T1L ≥ 1.25 µs`. The current driver's 0-bit cycle is 300 + 800 = 1100 ns,
which does not meet it (the 1-bit cycle, 1483 ns, does). Not known to be
causing a problem — the LEDs behave correctly — but it is out of spec and
should be corrected. See `firmware/ws2812.c`.
