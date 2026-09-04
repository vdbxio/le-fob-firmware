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
  INT goes only to **PA4 on the MCU**, which is likewise unpowered. SW1–SW4
  all land on the MCU too, so no button can reach it.

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

## Firmware gap

Nothing talks to the AW32001 at all today — there is no I²C driver. One would
want to:

- read status (charging, done, fault, VBUS present) for the battery reporting
  the status chirp will want;
- set `FET_DIS` deliberately rather than inheriting the default;
- leave the watchdog alone, or kick it, but not enable it and forget.
