# Running the I²C bus without R3/R4

The board's I²C pull-ups are 0201s and are easy to lose. Two ways to carry on
without them.

## Option 1 — solder new pull-ups to the castellations (preferred)

`SDA`, `SCL` and `3V3` are all brought out on the H1 castellations:

| Pad | Net |
|----:|-----|
| **H1.5** | SDA |
| **H1.6** | SCL |
| **H1.12** | 3V3 |

So a pair of ordinary 4.7k–10k resistors between **H1.5→H1.12** and
**H1.6→H1.12** restores a proper bus at a pitch you can actually solder,
without going near the 0201 pads. This is the option to take if the bus has
to be reliable.

## Option 2 — the CH592's internal pull-ups

Usable for bench work, with a caveat. The datasheet specifies `IUP`, the
input current of a GPIO with the built-in pull-up, as **25 / 60 / 90 µA** at
3.3 V — so the resistor is roughly:

| | |
|---|---|
| weakest (25 µA) | 132 kΩ |
| typ (60 µA) | 55 kΩ |
| strongest (90 µA) | 37 kΩ |

That is 5–20× weaker than a normal 4.7k, and rise time suffers accordingly.
To the ~0.7·VDD input threshold, with one device on a short trace:

| R | C=15pF | C=25pF | C=40pF |
|---|---|---|---|
| 37 k | 0.67 µs | 1.11 µs | 1.78 µs |
| 55 k | 0.99 µs | 1.65 µs | 2.64 µs |
| 132 k | 2.38 µs | 3.96 µs | 6.34 µs |

I²C standard mode allows `tr ≤ 1000 ns`, so this is 2–6× over spec **at
100 kHz**. But I²C has no minimum clock — the fix is simply to go slower and
hold SCL high long enough for the line to settle:

| bit rate | half period |
|---|---|
| 100 kHz | 5 µs |
| 50 kHz | 10 µs |
| **20 kHz** | **25 µs** |
| 10 kHz | 50 µs |

At 20 kHz the half period is 15× the worst-case rise, which is ample.

### What that requires of the driver

- **Bit-bang, not the hardware I²C peripheral.** The point is control over
  timing, and the peripheral does not offer enough of it.
- **True open-drain discipline.** Drive a line low by making the pin an
  output driving 0; release it by making it an *input with pull-up*. Never
  drive either line high — a slave holding it low would be fighting the
  driver.
- `power.c` already leaves PB14/PB15 as inputs with pull-up, so the bus idles
  high from reset. With R3/R4 missing that is now load-bearing rather than
  merely tidy.

### Where it will not do

Only for bench bring-up with the OLED unpopulated. Adding the display, or
wanting margin over temperature and part spread, means real pull-ups —
option 1.
