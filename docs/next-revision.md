# Next board revision — LED interface

Decision: **swap the PIXEL0 and PIXEL1 nets.** The four pad WS2812s move to
**PA14** (QFN pad 29), and the CON1 expansion header moves to **PB7** (pad 17).

Recorded here because the reasoning is easy to lose between the decision and
the layout, and because two details will bite if they are not carried across.

## Why

SPI0 MOSI — the only route to a DMA-driven WS2812 — is mappable to **PA14 or
PB14 only**. PB14 is I²C SDA (OLED + AW32001), so PA14 is the sole option.
The pads are the primary LED interface and should have the peripheral-backed
pin; an external strip on the header tolerates bit-banging far better than the
thing under the user's thumb.

The gain is not CPU time. Bit-banging four pixels costs ~106 µs of
interrupts-off per frame, about 0.04% of wall time when idle. What the swap
buys:

- **Immunity to a whole bug class.** Hardware generates the waveform; no
  compiler, pipeline or interrupt-masking involvement. Getting the bit-bang
  correct on WS2812B-V5 parts took four attempts — see the `T0H` margin
  discussion in `firmware/ws2812.c`.
- **Headroom.** Bit-bang scales linearly: 4 px = 106 µs, 16 px = 425 µs of
  interrupts-off per frame.
- **Sleep.** The CPU can `WFI` through a DMA transfer instead of spinning with
  interrupts masked.

## Detail 1 — ch32fun's stock SPI encoding is not V5-compliant

`extralibs/ws2812b_dma_spi_led_driver.h` encodes each WS2812 bit as 4 SPI bits
at 3 MHz (333 ns each). That is written for classic WS2812B and does not meet
the V5/V6 numbers this board uses:

| | stock SPI | WS2812B-V5 | |
|---|---|---|---|
| T0H | 333 ns | 220–380 | ok |
| T0L | 1000 ns | 580–1000 | at the limit |
| T1H | 1000 ns | 580–1000 | at the limit |
| T1L | **333 ns** | 580–1000 | **out of spec** |

This is the same failure that produced the original garbled output on the
bit-bang path: a `1` bit's *low* time is too short.

Fix — two changes, both small:

1. `R8_SPI0_CLOCK_DIV = FUNCONF_SYSTEM_CORE_CLOCK / 3333333;` (divider 18 at
   60 MHz → 3.333 MHz → 300 ns per SPI bit)
2. In the `bitquartets` table under `#ifdef CH5xx`, encode a `1` as two high
   bits instead of three: every `0b0111` nibble becomes `0b0011`. (The CH5xx
   table is bit-reversed, so this is `1100` on the wire, not `0011`.)

Result — inside **both** the V5 and the classic windows:

| | value | V5 | classic |
|---|---|---|---|
| T0H | 300 ns | 220–380 | 200–500 |
| T0L | 900 ns | 580–1000 | 650–950 |
| T1H | 600 ns | 580–1000 | 550–850 |
| T1L | 600 ns | 580–1000 | 450–750 |

## Detail 2 — PA14 sits near the antenna

PA14 is pad 29; the RF feed is pad 24, with an 8×3 mm keepout. The data line
will be carrying 3.33 MHz SPI edges. Route it away from the antenna feed and
its keepout — PB7 (pad 17) is on the opposite side of the package, so this is
a constraint the current layout never had to satisfy.

## Validate before committing the layout

PA14 already reaches CON1 pin 2 on the **current** board. Jumper CON1 pin 2 to
the pad chain's DIN and the SPI+DMA path can be proven — encoding fix
included — on hardware that already exists.

Worth doing while the layout is still open: it turns the respin from a change
made on expectation into one made on a measurement.

## Firmware impact

Small and contained. `firmware/ws2812.c` is already behind the `ws2812_init` /
`ws2812_show` interface in `ws2812.h`, and nothing above it knows how the bits
reach the chain. A second backend selected at build time is all that is
needed; `led_anim.c` and everything above it is unaffected.

`PIN_WS2812` in `board.h` becomes `PA14`, and the header net — no longer used
by this firmware — becomes PB7.
