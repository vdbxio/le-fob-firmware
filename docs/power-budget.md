# Power budget

## Measured 2026-09-04 — sleep confirmed working

52.5 s capture, PPK2 sourcing 4 V into the battery connector, buttons
exercised throughout.

| | |
|---|---|
| Asleep | 68.5% of the capture |
| **Sleep floor** | **~257 µA, flat** |
| Longest unbroken sleep | **12.8 s** with no periodic wakes |
| Awake mean | 3.9 mA |

The 12.8 s stretch is the important part: flat, no 1 kHz ripple, drifting
gently down from 259.8 to 256.1 µA. **The SysTick fix landed** — the chip
stays down instead of thrashing in and out every millisecond.

## Where the 257 µA goes

| | | |
|---|---|---|
| CH592 in our exact sleep config | 2.3 µA | datasheet Table 5-3 |
| AW32001 quiescent (max) | 38 µA | datasheet `IBAT_Q` |
| 4× WS2812B-V6 | 4 µA | ≤1 µA each |
| **Unaccounted** | **213 µA** | **83% of the floor** |

CH592 Table 5-3 gives the cost of each retention option, and our exact
combination — `RAM2K + RAM24K + CK32K(LSI) + CORE + EXTEND` — is the
**2.3 µA** row. So the MCU is ~1% of what is being drawn, and there is
nothing left to win in the sleep code. The remaining 213 µA is board-level.

### What 213 µA looks like

On an always-on 3V3 rail:

| | |
|---|---|
| 10k + 10k divider | 165 µA |
| 22k + 22k divider | 75 µA |
| 47k + 47k divider | 35 µA |
| CH592 internal pull-up into a held-low pin | 65–165 µA |

Three things worth checking, roughly in order:

1. **The NTC divider (R11/R12).** A fixed divider across a permanent rail
   draws continuously and never sleeps. If those are 10k, that is 165 µA on
   its own and most of the gap. Check the values and what they span.
2. **The LDO's own quiescent (PJ54M33).** Not yet measured or looked up;
   small regulators vary from ~1 µA to ~100 µA.
3. **An internal pull-up sourcing into something held low.** `power.c` puts
   pull-ups on unused pins, and `PA4` is the AW32001's **open-drain INT** —
   low whenever asserted. That one is ours, and it is testable in one flash:

   ```bash
   make clean && make EXTRA_CFLAGS="-ffreestanding -Iinclude \
       -DFOB_SLEEP_ENABLE=1 -DFOB_QUIESCE_PULLUPS=0" && make flash
   ```

   Floor drops → it was the pull-ups, and PA4 needs handling properly rather
   than blanket-quiescing. Floor unchanged → the load is passive board
   circuitry and the respin is where it gets fixed.

### What it is worth

On the 120 mAh cell, idle:

| | |
|---|---|
| Floor as measured | **19 days** |
| With the 213 µA found and removed | **113 days** |

## Earlier captures (void)

### 2026-09-03 breakdown

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

## Why it was not sleeping

Two faults in the sleep path, found by inspection after the capture showed a
`FOB_SLEEP_ENABLE=1` build sitting at the awake figure.

**SysTick was never masked.** `WFI` returns on any *pending* interrupt, and
SysTick fires at 1 kHz. So the chip entered deep sleep, was woken within a
millisecond, ran the loop and slept again — a thousand times a second. That
costs more than plain idling, and presents exactly as "sleep does nothing":
the floor stays at the awake value. `power_sleep()` now masks SysTick and
clears its pending flags before `WFI`, and restarts it after.

**No RTC interrupt handler existed.** `SleepInit()` enables `RTC_IRQn`, and
ch32fun's unhandled-interrupt default *spins forever*. `RTC_init()` arms a
trigger at count 0 and the counter wraps daily, so the fob would have hung
after about a day of running — and a pending RTC flag would equally have
stopped `WFI` from ever sleeping. There is now a handler that clears the
flag.

The first is why the measurement looked the way it did. The second is a
latent hang that the same review turned up.

> **The 40 nA figure below is not a valid measurement.** It was taken with
> the bench supply mis-wired to the battery terminals, so it measured
> something other than a sleeping fob. Both it and the 3.64 mA capture above
> need retaking before any conclusion about sleep is drawn — the sleep and
> wake fixes stand on their own reasoning but remain **unverified on
> hardware**.

### Then it slept, and would not wake

With those fixed the floor fell to **40 nA average** — and no button could
wake it. That is a third fault, and the opposite failure: too deep to
return from.

`buttons_init()` armed the wake source with `R8_SLP_WAKE_CTRL |=
RB_SLP_GPIO_WAKE` and nothing else. WCH's own `PWR_PeriphWakeUpCfg()` sets
two more things:

- **`RB_WAKE_EV_MODE`** — "0 = event keep valid for long time, 1 = short
  pulse event". A button edge *is* a short pulse. Without this bit the edge
  is never seen, and the only way out is a reset.
- **`RB_WAKE_DLY_MOD`** — how long to wait for clocks before running code.
  WCH's default is `01`, "short time, 520 cycles + TSUHSE", which lets the
  32 MHz crystal restart.

Arming the wake source now lives in `power.c` rather than `buttons.c`. Having
two files each own part of one configuration is how the incomplete version
went unnoticed.

Also added: `RB_PWR_MUST_0010` to the sleep power plan. The register map
documents that field as one that must be written with `0010`; WCH's
`LowPower_Sleep()` includes it as `(2 << 11)` and ch32fun's helper does not.

## Next step

Flash with sleep enabled and re-measure:

```bash
make clean && make EXTRA_CFLAGS="-ffreestanding -Iinclude -DFOB_SLEEP_ENABLE=1" && make flash
```

The boot banner states which build is running, so a capture cannot be
misattributed. Expect the floor to fall from 3.64 mA to under 0.2 mA.

If it still does not move, the next thing to check is what else is pending
when `WFI` runs — the radio's `LLE_IRQn` is the remaining enabled interrupt,
and the same "returns immediately on anything pending" rule applies to it.

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
