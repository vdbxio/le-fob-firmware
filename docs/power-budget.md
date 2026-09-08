# Power budget

## Confirmed 2026-09-04 — sleep and wake both working

69.6 s capture, PPK2 sourcing 4 V into the battery connector, NTC GND-side
resistor removed, charger driver off. Ten button events over the capture.

| | |
|---|---|
| **Sleep floor** | **35.9 µA**, mean over 51.5 s of sleep |
| Re-established after activity | yes, after every one of 10 wake events |
| Sleep dips *during* activity | none — lowest 1 ms inside an event is 1.3–2.2 mA |
| **Charge per press** | **~2 µAh** (press/double/triple); ~5 µAh for a long hold |

Both of the numbers that matter are now measured rather than predicted:
the floor *after* activity, and the cost of a press.

### Chirp cost, measured 2026-09-04

396 s capture with the 60 s chirp running: **4 chirps, each ~100 ms awake
and ~0.13 µAh** — roughly fifteen times cheaper than a press, since there
is no LED animation and only four burst rounds. Floor between events
28.5 µA.

| chirp interval | µAh/day for chirps | days |
|---|---|---|
| 30 s | 372 | 163 |
| 60 s | 186 | 138 |
| 5 min | 37 | 166 |
| 15 min | 12 | 172 |
| never | 0 | 175 |

(Floor alone is 684 µAh/day.) So a 60 s chirp costs about a fifth of the
lifetime and 5 min costs five percent. The interval is a product decision
about how stale a status may be, not a battery one — the earlier estimate
treating a chirp like a press overstated it by an order of magnitude.

### 2026-09-08 — the burst got five times longer; this table is stale

Delivery measured with 2 s chirps and Home Assistant's packet-id history
(several proxies within a few feet):

| burst | on air | delivered |
|---|---|---|
| 12 rounds × 20 ms | 240 ms | 31–36% |
| 60 rounds × 20 ms, +6 dBm | 1.2 s | 96% |
| 60 rounds × 20 ms, +7 dBm | 1.2 s | 98–99% |

Misses at 240 ms came in runs of up to 14 bursts: the whole burst fits in
one scan interval, so a window that misses takes every repeat with it. The
1.2 s span is now the default for presses and chirps alike. A follow-up run
at 12 rounds over the same 1.2 s (100 ms apart) also landed fine at these
receivers (ESPHome proxies at defaults plus the HA host's BlueZ adapter),
so the default round count is 20, a 60 ms gap, rather than 60.

**Projected cost, not yet measured.** The chirp above was ~100 ms awake for
0.13 µAh, i.e. ~4.7 mA while awake. Radio airtime is ~1 ms per round, so
sixty rounds at +7 dBm add only ~0.2 µAh; the price is 1.2 s awake instead
of 0.1 s:

| | per event | 60 s chirp, /day | 5 min chirp, /day |
|---|---|---|---|
| chirp, 4 rounds (measured) | 0.13 µAh | 186 µAh | 37 µAh |
| chirp, 60 rounds (projected) | ~1.8 µAh | ~2600 µAh | ~520 µAh |
| press (projected) | ~3.3 µAh | | |

Against the 684 µAh/day floor: a 60 s chirp now costs four floors a day
(dev only), and the 5 min production interval costs three-quarters of a
floor — roughly 100 days on the 120 mAh cell instead of 166.

**The way to get most of it back** is to sleep between rounds instead of
idling: the RTC can wake the chip every 20 ms, and the burst becomes sixty
short wakes at the ~36 µA floor rather than 1.2 s at ~5 mA. Each wake
re-initialises the radio (a few hundred µs), so the projected chirp comes
back down to a few tenths of a µAh. Not done yet; needs the PPK on it
before and after.

### Projected life, 120 mAh cell (12-round burst; superseded above)

| presses / day | days |
|---|---|
| 0 | 139 |
| 20 | 133 |
| 50 | 125 |
| 100 | 113 |
| 200 | 95 |

The idle floor dominates: 100 presses a day costs ~20% of the budget.
This is the baseline any scheduled chirp gets costed against — a chirp is
a wake plus a burst, so on the order of one press.

### What it took

For the record, the sleep/wake path needed six distinct fixes, each of
which was invisible from outside the chip and presented as "sleep does not
work" or "it stops after a press":

1. SysTick left armed across `WFI` — deep sleep lasted under a millisecond
2. no RTC interrupt handler — latent hang on the daily counter wrap
3. wake-mode bits set for pulse events on level-held buttons
4. pin configuration not restored after the wake's partial reset
5. a `Delay_Us` in the wake path before SysTick was restored — hang
6. `SLEEPDEEP` left set by ch32fun's sleep helper, so every idle `WFI`
   thereafter was a deep sleep with no restore — the LED corruption

All six are now pinned by `tests/test_power.c` against stubbed registers.

---

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

### The NTC divider

Awinic names this exact failure mode in the AW32001 pin description:

> *"Program the hot and cold temperature window with resistor dividers from
> VDD to GND, and NTC is the middle node. Pull NTC to VDD if NTC function is
> not used. If NTC function is unused, it is suggested to disable the NTC
> function and tie the NTC port to VDD **for decreasing leakage current of
> battery**, because PCB_OTP is default."*

A divider from VDD to GND is a permanent DC path — it draws whether the fob
is asleep, awake or in shipping mode. And the quiescent figure budgeted above
never covered it: `IBAT_Q` is specified *"not include the current from
external NTC resistor"*.

If the 213 µA gap is this divider, its total resistance is:

| VDD | R total |
|---|---|
| 3.3 V | 15.5 k |
| 3.7 V | 17.4 k |
| 4.0 V | 18.8 k |
| 4.2 V | 19.7 k |

— which is a 10k + 10k divider on the battery rail, almost exactly.

**Lift the GND-side resistor, not the VDD-side one.** That leaves NTC pulled
toward VDD through the remaining resistor, which is the configuration the
datasheet recommends for an unused NTC. Lifting the VDD-side one instead
pulls NTC to ground through the other, which reads as a fault.

Expect charging to be inhibited afterwards until `EN_NTC` is cleared over
I²C — NTC at VDD reads as maximally cold. That needs the charger driver that
does not exist yet, and is irrelevant to a no-USB sleep measurement.

**For the respin:** do not fit the divider at all. Tie NTC to VDD and clear
`EN_NTC` in firmware. It is the single largest item in the idle budget and it
costs a resistor to remove.

### Ruled out

- **`power.c`'s internal pull-ups.** Building with `FOB_QUIESCE_PULLUPS=0`
  made no measurable difference.
- **PA4 specifically.** Its jumper is currently DNP, so the pin is not
  connected to the charger's INT at all — the pull-up was sitting on an
  unconnected pad. This also means the shipping-mode `INT low for 2 s` wake
  path is not merely awkward on this board, it is absent: see
  [charger-power-path.md](charger-power-path.md).

### What it is worth

On the 120 mAh cell, idle:

| | |
|---|---|
| Floor as measured | **19 days** |
| With the NTC divider removed | **113 days** |

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
make clean && make SLEEP=1 flash
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
