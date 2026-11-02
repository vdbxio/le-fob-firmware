# AW32001 power path — notes

These notes were gathered while chasing a "dead on battery" symptom that
turned out to be a wiring mistake on the bench. **Nothing here diagnoses a
fault on this board.** They are kept because two of them are real latent gaps
worth settling during the power-path respin, and because the firmware will
need this material when it starts talking to the charger.

## Shipping mode has one usable exit on this board

The AW32001E has four states (§ *main state machine*, Figure 11), one of
which is **SHIP**: *"All paths are disabled"*, spec **≤1 µA** with
`VBAT=4.5 V, VIN=VSYS=0`. It is exited by exactly three things:

```
INT = Low for 2 s     ‖     VIN plug in     ‖     FET_DIS = 0  (I²C)
```

Of those, **only VIN plug-in is reachable on this board**:

- `FET_DIS=0` needs an I²C write, and in shipping mode the MCU has no power
  to make it.
- `INT = Low for 2 s` needs something to pull the charger's INT pin down.
  INT is routed only to **PA4 on the MCU** — and on the current board that
  jumper is DNP, so it reaches nothing at all. Even were it fitted, the MCU
  is unpowered in shipping mode, and SW1–SW4 all land on that same MCU.

Nothing currently sets `FET_DIS=1`, so the part should not be entering
shipping mode today. The gap is what happens if it ever does — a future
firmware using it deliberately, or a part that arrives in that state. The fob
would then be inert until USB is plugged, with no button able to help.

The datasheet adds a way to make that worse:

> *"If INT PIN is shorted to ground or **left floating** before entering
> Shipping Mode, DIS_SHIPINT (REG0CH[2]) must be written to 1 to avoid bad
> Shipping Mode operation. In this case, the only method of exiting shipping
> mode is plugging in the input adapter."*

With the MCU unpowered its pull-up on PA4 is gone, so the INT net floats —
the condition named. Worth checking on the schematic whether R13 is a series
resistor or a pull-up, and **what it references**: a pull-up to 3V3 or SYS is
dead in exactly the state where it would be needed. It would have to be BAT.

**For the respin:** if shipping mode is wanted — and for a product shipping
with a cell installed it is genuinely useful — then something has to be able
to wake it. A button able to pull INT low, or one of the existing switches
wired to INT as well as to the MCU. If it is not wanted, that is worth
deciding explicitly rather than by omission.

## The watchdog drops the system rail

> *"In both charge and discharge mode, once the watchdog timer runs out, both
> the Q1 and Q2 are turned off, and most registers return to the default
> value. The Q1 and Q2 are turned on again automatically after tRST_DUR."*

Q2 off means SYS collapses and the MCU reboots. The watchdog is off in
discharge mode by default (`REG05H[7]`), so this is not currently live — but
any firmware that enables it must then kick it (`REG02H[6]`), and a fob that
mysteriously reboots every 40–160 s on battery would be this.

## PCB_OTP turns off *both* FETs

Worth knowing before touching the NTC network, because it presents as a
completely dead board:

> *"When PCB_OTP is selected and the NTC voltage is lower than the NTC hot
> threshold, **both the Q2 and Q1 are turned off**. The NTC_FAULT status is
> set (REG09H[1])… The IC Operation resumes when the NTC voltage returns to
> safe range."*

Q1 is the IN→SYS path and Q2 is BAT→SYS, so a low NTC kills *both* — the
board will not run from the battery **or** from USB. It is not latched; it
recovers as soon as NTC is back in range.

And PCB_OTP is what is selected out of the box. Table 5:

| EN_NTC | DIS_PCB_OTP | Function |
|---|---|---|
| 0 | x | Disabled |
| 1 | 1 | NTC (resistor-divided) |
| **1** | **0** | **PCB_OTP** ← default |

So removing the NTC divider without tying the pin somewhere leaves NTC low
or floating, trips PCB_OTP, and the board draws only leakage.

### Getting a sleep measurement without firmware

Clearing `EN_NTC` needs I²C, which needs the MCU, which needs SYS — and SYS
is exactly what the fault turned off. There is no way out of that in
firmware.

The hardware route works and is what the datasheet recommends anyway: **tie
NTC to VDD**. Refit the VDD-side resistor and leave the GND-side one off, or
bridge NTC to VDD directly. Then:

- no divider current, which was the point;
- NTC high, so PCB_OTP does not trip and both FETs stay on;
- charging inhibited, because NTC at VDD reads as maximally cold — but *"the
  NTC function works only in charge mode"*, so discharge is unaffected and
  the sleep figure is measurable.

Charging comes back once firmware can clear `EN_NTC`.

## NTC leakage

The NTC divider is a permanent DC path from VDD to GND and is the largest
single item in the idle budget — see
[power-budget.md](power-budget.md). Awinic's own guidance is to omit it when
no thermistor is used: tie NTC to VDD and clear `EN_NTC`. Both halves matter,
and the firmware half needs the driver below.

## Firmware

`firmware/charger.c` now drives the part; see the README's *Charging*
section. Two things from these notes that it relies on: the watchdog is
disabled outright rather than kicked, and `FET_DIS` is force-cleared on
every REG06H write, so firmware never puts the board into shipping mode.

The one thing these notes missed: REG01H resets with **CEB = 1, charging
disabled**. Until firmware clears it the part only powers the system from
USB.
