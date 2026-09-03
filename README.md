# Le'Fob — CH592X BTHome firmware

Four-button BLE fob on a WCH **CH592X** (QFN-32). Each pad broadcasts
[BTHome v2](https://bthome.io/format/) button events and lights the WS2812
sitting underneath it.

- **Gestures:** press, double, triple, long, long-double, long-triple, hold
- **LEDs:** 4× WS2812 on the PIXEL0 net, bit-banged, one per pad
- **Radio:** raw BLE advertising via ch32fun's iSLER — no BLE stack
- **Interrupt-driven** throughout, with the deep-sleep wake path already wired

## Pin map

Taken from the CH592 datasheet pin table (CH592DS1 V1.9 §1.2, **CH592X**
column) cross-referenced with the board netlist.

| QFN pad | Net | GPIO | Role |
|--------:|-----|------|------|
| 20 | BOOT | **PB22** | SW1 — also the ROM bootloader probe |
| 18 | BTN2 | **PB4** | SW2 |
| 19 | BTN3 | **PB23** | SW3 — also RST# |
| 10 | BTN4 | **PA9** | SW4 |
| 17 | PIXEL0 | **PB7** | WS2812 chain W1→W2→W3→W4, order matches SW1→SW4 |
| 29 | PIXEL1 | PA14 | external header CON1 (unused) |
| 3 / 2 | PWM6 / PWM8 | PB0 / PB6 | white LED and IR gates (unused) |
| 12 / 11 | SDA / SCL | PB14 / PB15 | I²C — OLED, AW32001 charger (unused) |

All four switches short to ground, so they are active-low with the internal
pull-up enabled.

## Two pads that are not just pads

**SW3 / PB23 is RST# out of the factory.** Until the `CFG_RESET_EN` option
bit is cleared, pressing SW3 resets the chip instead of registering a press —
which looks exactly like a crash if you are not expecting it. Once per board:

```bash
make disable-reset
```

`make flash` runs this for you, so in practice you only need it if you flash
some other way.

**SW1 / PB22 is the bootloader probe.** Holding it while USB is plugged in
enters ISP mode rather than running this firmware. That is the flashing
gesture; in normal operation PB22 is just button 1.

## Build and flash

```bash
brew install riscv64-elf-gcc libusb pkg-config
make
```

To flash: hold **SW1**, plug in USB, release **SW1**, then

```bash
make flash
```

That first runs `minichlink -D`, the PB23 reset-disable described above.
minichlink implements it over the ISP transport and checks the current
setting before writing, so re-running it on every flash costs nothing.

If a factory-fresh part refuses to program over USB ISP because the debug
module is still enabled, there is a separate target:

```bash
make disable-debug
```

This is deliberately **not** part of `make flash`. It prompts for
confirmation twice — which would wedge an automated build — and it is close
to one-way: afterwards the chip can only be programmed through the
bootloader, and `printf`-over-SWIO stops working permanently. Run it by hand,
once, knowingly.

> If minichlink warns *"you have debug enabled… Proceeding will only erase
> the flash"*, stop and re-run — continuing erases without programming.

## Console

`printf` goes out over the SWIO debug interface:

```bash
make console
```

The boot banner reports the advertising address (and whether it came from the
factory or had to be synthesised), the pin map, and a reminder about SW3.
Every gesture prints one line.

Two caveats worth knowing before you rely on this:

- It needs the debug module, so it stops working for good once
  `make disable-debug` has been run on a board.
- With nothing attached, the *first* `printf` spins for ~120 ms waiting for a
  debugger, then latches into a fast no-op for the rest of the run. It costs
  one 120 ms stall at boot and nothing after that.

If you want logging on a board with debug disabled, the native USB CDC path
is the way — CH592 has native USB and ch32fun ships a CDC device stack.

### About the toolchain

Homebrew's `riscv64-elf-gcc` is a compiler with no C library, so there is no
`stdio.h` or `string.h` on the include path. That turns out to be nearly
harmless, because ch32fun builds `-nostdlib` and carries its own `printf`,
`memcpy` and friends — only the declarations were missing. `firmware/include/`
supplies them and the build adds `-ffreestanding`. See
[`firmware/include/README.md`](firmware/include/README.md) for the details and
for how to switch to a full newlib toolchain instead.

## Home Assistant

Settings → Devices & Services → **Add integration** → **BTHome**, then press a
button. The fob advertises as `le-fob` and appears as four button entities.
No encryption key — this is plaintext BTHome v2.

Each gesture maps to a BTHome button event on object `0x3A`:

| Gesture | BTHome event | LED |
|---------|--------------|-----|
| tap | `0x01` press | 1 flash, pad colour |
| tap ×2 | `0x02` double_press | 2 flashes |
| tap ×3 | `0x03` triple_press | 3 flashes |
| hold ≥600 ms | `0x04` long_press | white fade |
| tap then hold | `0x05` long_double_press | white fade |
| tap ×2 then hold | `0x06` long_triple_press | white fade |
| hold ≥1500 ms | `0x80` hold_press | red pulse |

Timings live in [`firmware/board.h`](firmware/board.h).

## Pad LEDs

The pads are **dark at rest**. Three layers can light one, highest priority
first:

| Layer | When | Looks like |
|-------|------|-----------|
| Gesture | a gesture is decoded | the patterns above, at the full ceiling |
| On air | a burst is transmitting | steady white |
| Press | a pad is physically held | steady, dim, pad colour |

Pad identity colours are cyan / green / amber / magenta for SW1–SW4.

The press layer exists because a single tap is not *decoded* until the
multi-tap window closes, ~280 ms after release. Without it the fob feels
dead under your thumb. It lights on the debounced press, so ~8 ms in.

A running gesture animation owns its pixel outright, dark phases included, so
the lower layers cannot fight its blink pattern.

**On the "on air" layer:** every advertisement today is triggered by a
gesture, and the gesture pattern runs concurrently with the burst — so that
pattern already *is* the on-air indication, and the white glow is suppressed
on that pad. Without the suppression a single tap would show a stray white
tick, because one blink is 210 ms against a 240 ms burst. What is left is the
case the layer is for: a chirp with no gesture behind it. There are none yet
— battery or status chirps would be the first — so on current firmware the
white glow is unused by design, and wired up ready.

### Brightness

`PAD_LED_BRIGHTNESS_PCT` (**10%**) is the single ceiling; every colour any
layer produces is scaled by it. `PAD_LED_PRESS_PCT` and `PAD_LED_TX_PCT` are
percentages *of* that ceiling, so moving the ceiling moves everything
together. `PAD_LED_SHOW_PRESS` and `PAD_LED_SHOW_TX` drop either layer
entirely. All in [`firmware/board.h`](firmware/board.h).

## How it fits together

```
GPIO falling-edge IRQ  ──►  wake + "something happened" flag   buttons.c
SysTick @ 1 kHz        ──►  tick flag                          lefob.c
main loop              ──►  debounce → gestures → LEDs → radio
```

No decoding happens in an interrupt. The ISRs set a flag and return; the
gesture state machine, the WS2812 bit-bang and the radio all run from the
main loop. That keeps interrupt latency to a few instructions and means the
LED refresh — which runs with interrupts disabled — can never pre-empt a
button edge.

Debouncing is done against absolute timestamps rather than by counting
samples, so a tick delayed by a BLE burst does not stretch the debounce
window.

### Interrupts and the QFN-32 package

PB22 and PB23 have no bits of their own in the 16-bit `R16_PB_INT_*`
registers. WCH routes them through interrupt lines INT24/INT25, which surface
as PB interrupt bits **8 and 9** — bits that are otherwise unused here,
because CH592X has no PB8/PB9 pads. So:

- `R16_PIN_ALTERNATE` must have `RB_PIN_INTX` set, or those bits listen to
  nothing at all on this package
- the *interrupt* registers are addressed by the remapped bit (8/9) while the
  *pad* registers are addressed by the real bit (22/23)

This matches `GPIOB_ITModeCfg()` in WCH's own `CH59x_gpio.c`, which computes
the same remap. Edge polarity is not a field in the interrupt registers
either: in edge mode the pin's *output latch* selects which edge, which is
why the driver writes `R32_PB_CLR` to select falling.

### Sleep

Off by default (`FOB_SLEEP_ENABLE` in `board.h`) — a chip that drops into deep
sleep is harder to talk to during bring-up. The wake path is built and
compiled either way: the GPIO edge interrupts and `RB_SLP_GPIO_WAKE` are armed
in `buttons_init()` regardless, and `lefob.c` has the `LowPowerSleepWFI()` call
behind that one flag. Both configurations are known to build.

The loop only considers sleeping once no pad is down, no gesture is
mid-decode, no animation is running and no advertising burst is outstanding,
plus `FOB_IDLE_LINGER_MS` of slack so a user between taps of a double-press is
never cut off.

## Tests

`tests/` compiles the target's own `bthome.c` and `buttons.c` natively
against a stub `ch32fun.h`, so both are checked by running them rather than
by reading them. Nothing is reimplemented.

```bash
make -C tests
```

**Packet layout** — checked against the BTHome spec: lengths, the service
data UUID and device-info byte, ascending object IDs, the 31-byte payload
limit, and the advertiser-address byte-order handling in all three of its
cases.

**Pad LEDs** — `ws2812_show` is stubbed to capture the framebuffer, so the
real animation engine can be driven through its states: dark at rest, the
press glow and its colour, gesture patterns outranking it, the on-air glow
appearing for a gesture-less burst but suppressed for a gesture-driven one
(including after the pattern ends), the brightness ceiling hit exactly, and
that idle frames do not touch the chain at all.

**Gestures** — the stubbed GPIO registers let the test drive the pads
millisecond by millisecond and assert on the event queue: every tap and hold
combination including the banked-tap `long_double` / `long_triple` paths,
that a long press fires while still held rather than on release, that a 3 ms
glitch is rejected and a bouncing contact still reads as one tap, that pads
are independent, and that `buttons_init()` leaves `RB_PIN_INTX` set with the
PB22→bit 8 and PB23→bit 9 remap armed.

## Layout

```
firmware/
  lefob.c        entry point, SysTick, main loop
  board.h        pin map and every tunable
  buttons.c/h    GPIO edge IRQs, debounce, gesture state machine
  ws2812.c/h     bit-banged WS2812 on PB7, runs from RAM
  led_anim.c/h   per-pixel layering: gesture, on-air, press
  bthome.c/h     BTHome v2 packet builder
  ble_adv.c/h    iSLER advertising bursts
  libc_shim.c    one strong memset; see the file for why
  include/       freestanding libc headers for Homebrew's GCC
tests/           host-side packet tests
```
