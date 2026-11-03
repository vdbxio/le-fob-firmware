# Le'Fob — CH592X BTHome firmware

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![MCU: WCH CH592X](https://img.shields.io/badge/MCU-WCH%20CH592X-informational)](#pin-map)
[![BTHome v2](https://img.shields.io/badge/BTHome-v2-brightgreen)](https://bthome.io/format/)
[![built on ch32fun](https://img.shields.io/badge/built%20on-ch32fun-orange)](https://github.com/cnlohr/ch32fun)
[![tests: 11 host suites](https://img.shields.io/badge/tests-11%20host%20suites-success)](#tests)
[![vibe coded](https://img.shields.io/badge/vibe%20coded-with%20Claude-d97757?logo=anthropic&logoColor=white)](https://claude.com/claude-code)

Four-button BLE fob on a WCH **CH592X** (QFN-32). Each pad broadcasts
[BTHome v2](https://bthome.io/format/) button events and lights the WS2812
sitting adjacent to it. Open hardware from Voidbox Industries; this is the firmware.

- **Gestures:** press, double, triple, long, long-double, long-triple, hold
- **LEDs:** 4× WS2812 on the PIXEL0 net, bit-banged, one per pad
- **Radio:** raw BLE advertising via ch32fun's iSLER — no BLE stack
- **Status chirp** every 5 minutes: die temperature, supply voltage,
  battery low, charging
- **Charging:** the AW32001 ships with charging disabled; the firmware
  turns it on and pad 1 breathes magenta while it charges
- **Flashlight:** SW4 drives the two white LEDs, with a dimmer while latched
- **Deep sleep by default:** a measured 36 µA floor, months on a 100 mAh cell

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
| 3 / 2 | PWM6 / PWM8 | PB0 / PB6 | flashlight: one white LED per FET channel (the PWM8 pad doubles as the IR footprint) |
| 12 / 11 | SDA / SCL | PB14 / PB15 | I²C — AW32001 charger, OLED (unused) |

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

**…but only at reset.** The ROM samples BOOT once, when the chip comes up.
With no battery, plugging USB *is* a power-up, so hold-and-plug works. With a
battery fitted the chip never loses power, USB does not reset it, and the
hold does nothing — the running app just sees button 1 go down. So the app
provides its own way in:

**Hold SW1 alone for 5 seconds.** From 2 s the pads fill in white one at a
time (SW1 → SW4) as a warning; let go and nothing happens. At 5 s all four
flash white and the app jumps into the ROM bootloader, exactly as if it had
been reset with SW1 down. Then `make flash` as usual.

The jump is refused — three red flashes — if the charger reports no USB,
because the bootloader is useless without it and the only way back out is a
reset. If the charger driver is off or not answering, the check is skipped.
Knobs: `ISP_HOLD_MS=` and `ISP_REQUIRE_USB=0`.

If the firmware on the board predates this gesture, disconnect the battery
once and use hold-and-plug.

## Build and flash

macOS:

```bash
brew install riscv64-elf-gcc libusb pkg-config
make
```

Linux (Fedora shown; Debian names differ):

```bash
sudo dnf install libusb1-devel systemd-devel
# any RISC-V bare-metal GCC on PATH works; ch32fun picks up the prefix.
# The xPack riscv-none-elf-gcc release tarball is the easy one.
sudo cp tools/70-wch-isp.rules /etc/udev/rules.d/ && sudo udevadm control --reload
make -C ch32fun/minichlink
make
```

The udev rule lets a non-root user talk to the WCH ISP bootloader
(USB `4348:55e0`).

To flash: hold **SW1**, plug in USB, release **SW1**, then

```bash
make flash
```

### A brand-new board

```bash
make fresh
```

Same SW1-hold gesture. Runs the two one-time steps a factory-fresh CH592
needs — `-D` for the PB23 reset-disable, and `-n` to disable the debug module,
without which a fresh part refuses to program over USB ISP — then flashes.

`-n` prompts for confirmation twice and is close to one-way: afterwards the
chip programs only through the bootloader, and **`make console` stops working
on that board permanently**. That is why `make flash` does neither half. Use
`make fresh` once per new board, `make flash` from then on.

That first runs `minichlink -D`, the PB23 reset-disable described above.
minichlink implements it over the ISP transport and checks the current
setting before writing, so re-running it on every flash costs nothing.

`make disable-debug` does the `-n` step alone, if you want it without
reflashing.

> If minichlink warns *"you have debug enabled… Proceeding will only erase
> the flash"*, stop and re-run — continuing erases without programming.

### Build knobs

All of these are `make VAR=value flash` (always with `make clean` first if
you are changing one — the knob is baked into every object). Defaults are in
`firmware/board.h`, with the reasoning next to each.

| Knob | Default | What it does |
|---|---|---|
| `SLEEP=0` | 1 | Stay awake instead of deep-sleeping between events |
| `CHIRP_MS=2000` | 300000 | Status chirp interval (5 min); `0` disables timed wakes |
| `CHIRP_TEXT=0` | 1 | Chirp carries the button frame instead of the status objects |
| `CHARGER=0` / `CHARGER_INT=0` | 1 / 1 | Charger driver / charger INT pin |
| `CHG_MA=96` | 48 | Battery charge current in mA, multiple of 8 |
| `LED_CHARGE=0` | 1 | Pad 1 charge indicator while on USB |
| `FLASHLIGHT_PCT=30` / `FLASHLIGHT=0` | 20 / 1 | Torch momentary and latch-start duty / torch at all |
| `FLASHLIGHT_MAX_PCT=100` | 90 | Cap for the latched brightness steps (bench only) |
| `FLASHLIGHT_CHANNELS=1` | 3 | Torch FET channels: 1 = PWM6, 2 = PWM8, 3 = both |
| `RADIO=0` | on | No transmissions at all (LED diagnostic build) |
| `TXPOWER=4` | 7 | Transmit power in dBm, −25 … +7 |
| `ROUNDS=12` | 20 | Repeats per burst, presses and chirps alike |
| `BURST_MS=1200` | 1200 | Time on air; the repeats are spread evenly across it |
| `HSE_CAP=12` | chip reset | 32 MHz crystal load trim in pF, even values 10–24 |
| `ISP_HOLD_MS=` / `ISP_REQUIRE_USB=0` | 5000 / 1 | Bootloader gesture length / VBUS check |
| `SETTLE_US=` / `PULLUPS=` | 250 / — | Wake settling delay / quiesce mode for unused pins |
| `ENCRYPT=0` | 1 | Plaintext BTHome, frames exactly as the Supercon units sent them |
| `HID_KEYPAD=0` | 1 | Type the key's digits from the number row instead of the keypad |

**Delivery-rate rig.** The packet id in Home Assistant counts every burst, so
a fast chirp with sleep off is a direct measurement of how many land:

```bash
make clean && make SLEEP=0 CHIRP_MS=2000 flash
```

Export the packet-id history for a couple of minutes; received ÷ (last −
first + 1) is the delivery rate. Change one knob at a time and re-run. The
banner also prints the crystal trim the chip is actually using (`xtal`).

## Charging

The AW32001 **does not charge out of reset**: REG01H resets with CEB = 1,
"charge disable", and it stays that way until a host clears it. With USB in
it powers the board and leaves the battery alone. Firmware before the fix
never cleared it, which is why the status text only ever said `USB idle`.

`charger.c` now applies four corrections at boot, each one read back to
confirm it took, in this order:

| | Register | Reset value | Set to |
|---|---|---|---|
| 1 | REG05H watchdog | 160 s | off: a sleeping fob cannot kick it |
| 2 | REG06H EN_NTC | on | off: NTC tied high reads as "too cold" |
| 3 | REG02H ICHG | 128 mA (1.3C) | `CHG_MA`, default 48 mA (~0.5C on 100 mAh) |
| 4 | REG01H CEB | 1, charging off | 0, charging on |

Every chirp also checks that CEB and the watchdog are still where they were
left, and re-applies the lot if the part has reverted to defaults (watchdog
expiry, battery re-insert). It also retries a charger that was not found at
boot.

The status text in Home Assistant reads `USB chg` while charging, `USB done`
when full, `USB pre` while pre-charging a deeply flat cell, and `battery`
unplugged. `chgr cfg!` means a write did not verify: the part was put back
to defaults and **will not charge**. The next chirp tries again.

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
supplies them and the build adds `-ffreestanding`. A full toolchain such as
the xPack `riscv-none-elf-gcc` builds the same tree unchanged. See
[`firmware/include/README.md`](firmware/include/README.md) for the details.

## Home Assistant

Home Assistant finds the fob by itself under Settings → Devices & Services →
**Discovered**. It is encrypted (see [Encryption](#encryption)), so HA asks
for a **bindkey**:

1. Plug the fob into the computer you are using HA on and leave it there.
2. Click into the bindkey field.
3. Hold **SW2** (second from the USB port) for 5 s. The pads fill cyan one
   by one; let go early to cancel. A moment later the fob types the 32
   characters into the field.
4. Submit. Four button entities and the status entities below appear.

A plaintext build (`make ENCRYPT=0 flash`) behaves as the Supercon units
did: **Add integration** → **BTHome**, press a button, no key.

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

### Status chirp

Every 5 minutes, once at boot, and whenever USB goes in or out, the fob
sends its status with no button objects. Encrypted, that no longer fits in
one 31-byte advertisement, so it goes out as two frames, each with its own
packet id, and HA merges them into the one device:

| Frame | Object | Entity | Meaning |
|---|---|---|---|
| power | `0x02` temperature | Temperature | the CH592 die, ± a few °C; left out if the sensor fails |
| power | `0x0C` voltage | Voltage | the 3V3 rail, **not the cell** — see below |
| power | `0x15` battery | Battery (binary) | on = low, charge me |
| power | `0x16` battery charging | Battery charging | on while the cell is charging |
| power | `0x3D` count | Count | bindkey readouts: see [Encryption](#encryption) |
| text | `0x53` text | Text | `USB chg`, `USB done`, `battery`, `fault …`, or `chgr …` (11 chars max encrypted) |

The power frame is full at 31 bytes. More status on a later board (a fuel
gauge) gets a third frame; the radio code already queues frames in order.
The plaintext build sends the single combined frame it always did.

The board has no battery voltage sense. What the chip can measure is its own
supply, which the 3.3 V regulator holds steady until the cell can no longer
keep it in regulation (roughly 3.35 V, the last few percent of a LiPo). So
the voltage reads flat for weeks and then falls; **battery low** fires once
it sits 60 mV under the highest reading seen since boot. That leaves a
couple of days of chirps at the 5 min interval — enough for an alert, not a
fuel gauge.

Encrypted frames have no room for the local name, so HA names the fob
"BTHome sensor" plus the last four digits of its address until you rename
it. (The plaintext build still carries `le-fob` in button frames.)

## Encryption

BTHome v2 AES-CCM, on by default. In short:

- **The key is born on the fob.** On first boot, or the first after its
  DataFlash is erased, the fob fills a pool from temperature-sensor ADC noise
  and the beat between its two crystals, mixes in its factory MAC (so no two
  fobs can ever share a key, even if the noise were identical), and
  conditions it with AES-CBC-MAC on the chip's AES block. Nobody else ever
  has the key; there is no list.
- **Reading it out:** hold SW2 alone for 5 s while enumerated on USB (cyan
  fuse); the fob types 32 lowercase hex characters into whatever has focus,
  and nothing else. Digits go out as keypad keys, which work on every
  keyboard layout; if the host reports NumLock off, the fob presses NumLock
  before and after. The hold must start *after* the fob is plugged in.
- **Exposure count:** every readout bumps the `0x3D` count *before* typing.
  After setup it reads 1, or 2 if you also saved a copy. If it ever goes up
  on its own, rekey. Build an HA alert on it; nobody watches a count sensor
  unprompted.
- **Rekey:** hold SW2 + SW3 for 10 s on USB (red fuse). New key, exposure
  count back to 0, then HA needs the new key. The counter keeps climbing, so
  nothing sent under the old key replays under the new one.
- **Replay counter:** starts at 1000 (HA exempts counters below 100 from its
  replay check), takes a fresh value for every advertising round, and only
  ever goes up — across sleep, flat batteries, reboots and firmware updates.
  It is persisted as a reservation 1024 values ahead in a journaled
  DataFlash ring, topped up when it runs low, never while the torch is on,
  and resumed from the highest reservation ever written. A fob that cannot
  persist its counter sends nothing.

What it does not do: hide the MAC (BTHome puts it in the nonce, so it cannot
rotate), or stop someone holding the fob from reading the key — that is the
trade for no app and no QR code, and why the exposure count exists. The
design and its reasoning are in the MVP spec.

**Not yet checked on hardware:** whether `make flash` (WCH ISP) leaves
DataFlash alone. If it erases it, every reflash makes a new key and needs
re-pairing.

## Flashlight

SW4 (furthest from the USB port) drives the two white LEDs, one on each
channel of Q1 (PWM6 → Q1-B, PWM8 → Q1-A):

| Gesture | Light |
|---|---|
| press and hold | on while held at 20%, off on release |
| double tap | latched on at 20% |
| SW2 / SW3 while latched | 10% dimmer / brighter, 10% … 90% |
| any press of SW4 while latched | off (that press does not relight it) |
| latched 5 minutes | off by itself |

The BTHome events for SW4 still go out as normal, so the pad can drive an
automation as well. SW2 and SW3 presses made while the light is latched
are the dimmer and stay off the air; unlatched they are ordinary remote
buttons again. Every new latch starts back at 20%.

**There is no current limiting on this board revision.** LED1 and LED2 sit
across the system rail through the FET, so the PWM duty in
[`flashlight.c`](firmware/flashlight.c) is the only thing limiting their
current. `FLASHLIGHT_PCT` (**20%**, 919 Hz) is the momentary and
latch-start level; `FLASHLIGHT_MAX_PCT` (**90%**) is the hard cap for the
latched steps, and nothing in the firmware drives the LEDs harder. On the
bench 90% runs barely warm; 100% has not been tried.

The two channels are **interleaved** — PWM8 runs inverted with the
complementary count, so one LED is on while the other is off. Below 50%
that halves the peak current and the rail sag for the same light; above
50% the two on-times overlap in the middle of the cycle.

Knobs: `make FLASHLIGHT_MAX_PCT=100 flash` raises the cap knowingly,
`make FLASHLIGHT=0 flash` removes the feature, and a board reworked with
the IR emitter on Q1-A's pad runs the torch on PWM6 alone with
`make FLASHLIGHT_CHANNELS=1 flash`.

The light is refused while the battery reads low, so a torch cannot
flatten a fob past the point where it can still advertise. The fob does
not deep-sleep while the light is on.

## Pad LEDs

The pads are **dark at rest**. Three layers can light one, highest priority
first:

| Layer | When | Looks like |
|-------|------|-----------|
| Gesture | a gesture is decoded | the patterns above, at the full ceiling |
| On air | a burst is transmitting | steady white |
| Press | a pad is physically held | steady, dim, pad colour |
| Charge | USB is in (pad 1 only) | magenta breathing while charging, steady green when full, slow red blink on a charger fault |

While USB is in the fob does not deep-sleep — it is being powered from the
cable — and re-reads the charger once a second, so the pad 1 indicator and
the `charging` entity follow a plug or unplug within a second even on boards
without the INT jumper. Unplug and it goes back to sleeping as normal.
`make LED_CHARGE=0 flash` drops the indicator (the fob still stays awake on
USB).

Pad identity colours are cyan / green / amber / magenta for SW1–SW4.

The press layer exists because a single tap is not *decoded* until the
multi-tap window closes, ~280 ms after release. Without it the fob feels
dead under your thumb. It lights on the debounced press, so ~8 ms in.

A running gesture animation owns its pixel outright, dark phases included, so
the lower layers cannot fight its blink pattern.

**On the "on air" layer:** a gesture pattern runs concurrently with its
burst, so that pattern already *is* the on-air indication, and the white
glow is suppressed on that pad. Without the suppression a single tap would
show a stray white tick, because one blink is 210 ms against a 1.2 s burst.
Status chirps deliberately light nothing: a fob chirping in a drawer every
five minutes should not flash. So on current firmware the white glow never
shows; the layer is wired and tested for whatever gesture-less burst comes
next.

### Driving the WS2812s

The pads are **WS2812B-V5/V6**, whose timing is not the classic WS2812B
timing. From the V5 datasheet:

| | V5/V6 | classic WS2812B |
|---|---|---|
| T0H | 220 ns – 380 ns | 200 – 500 |
| T0L | 580 ns – 1 µs | 650 – 950 |
| T1H | 580 ns – 1 µs | 550 – 850 |
| T1L | **580 ns – 1 µs** | 450 – 750 |
| RES | > 280 µs | > 50 µs |

The trap is `T1L`. Classic WS2812B has a fixed ~1.25 µs bit period, so
driving it as "period minus high time" is fine. V5 does not — a `1` bit is
580 ns high *and* 580 ns low at minimum, so its period is genuinely longer
than a `0` bit's. Deriving the low time from a fixed 1.25 µs period gives
`T1L = 550 ns`, under the V5 minimum: every `1` bit out of spec while every
`0` bit still looks fine.

So [`ws2812.c`](firmware/ws2812.c) specifies all four times independently and
lets each bit's period be whatever they add up to. The values chosen
(250 / 800 / 700 / 800 ns) sit inside both columns, so the driver works on
either variant, and `_Static_assert`s fail the build if any of them is tuned
outside the V5 windows.

**The high and low phases are timed by different mechanisms, deliberately.**
Waiting on the SysTick counter is self-correcting and independent of what the
compiler emitted, but it samples in a loop of a few instructions, so an edge
lands somewhere in a 50–100 ns window after its deadline. Against T0L, T1H
and T1L — all 420 ns wide — that is nothing. `T0H` is different: 220–380 ns
is only 160 ns wide, so that jitter is a large fraction of it, and a `0` bit
misread that way corrupts the rest of the frame, making the pixels
*downstream* of the intended one flash.

So the high phase is a **computed jump into a sled of nops** — a `1` bit
enters at the top and runs all 39, a `0` bit enters 24 in and runs 15 — and
the low phase is a SysTick wait against the slot boundary, absorbing all
accumulated error into the parameter with room for it.

Getting the high phase right took three attempts, and the two failures are
worth recording because they fail the same way — something whose cost cannot
be pinned down ends up *between the two stores*:

```c
if (one) { nops } else { nops }     // compiler hoists the rising-edge store
                                    // above the branch and shares the falling
                                    // one, so an arm pays a taken branch in
                                    // and a taken jump out, inside the pulse
```

A computed jump into a nop sled fixes the asymmetry — both bit values cost
the same — but that cost includes a `jr`, and what a jump-register costs on
this core is undocumented; it may flush the pipeline. Sizing the nops then
means guessing, over a ~100 ns spread that `T0H`'s 160 ns window cannot
absorb.

What works is **each bit value in its own fully duplicated arm**, with the
test before the rising edge and the join after the falling edge:

| | branch | rise | pulse | fall | join |
|---|---|---|---|---|---|
| `1` bit | `beqz` not taken | `sw` | 40 nops | `sw` | `j` |
| `0` bit | `beqz` taken | `sw` | 16 nops | `sw` | — |

Nothing but nops sits between the two stores, so the high time is `N + 1`
cycles and nothing else — a number that can be stated rather than estimated:

| | cycles | | spec | margin |
|---|---|---|---|---|
| T0H | 17 | 283 ns | 220–380 | −63 / +97 ns |
| T1H | 41 | 683 ns | 580–1000 | −103 / +317 ns |

It has to be one `asm` block. Written as C, the compiler merges the arms back
together and reintroduces the problem.

**If a pad LED ever glitches again**, the tell is *which* pixel. Pixel 0 is
the only one fed the raw signal from PB7 — every other pixel sees a waveform
regenerated by the one before it — so a pixel-0-only artifact points here,
and anything else points at the layering. To rule the radio in or out first,
see the diagnostic build above.

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

**On by default** — a plain `make flash` gives you the sleeping build. It was
off through bring-up, but it has been measured working — a 36 µA floor,
re-established after every wake, with a press costing about 2 µAh and a
chirp about 0.13 µAh; see [`docs/power-budget.md`](docs/power-budget.md) —
and it is the difference between a fob that lasts a day and one that lasts
months. Two fobs ran over a month on 100 mAh cells on the 5 minute chirp.

For bring-up work where you would rather the chip stayed awake:

```bash
make clean && make SLEEP=0 flash
```

The boot banner states which build is running.

[`power.c`](firmware/power.c) does the setup this needs: the internal DC-DC
(roughly 60% of the LDO's active current), the LSI for the 32 kHz domain, the
RTC, and quiescing every pin nothing else owns. That last part is what
separates a single-digit-microamp sleep from a tens-of-microamps one — a
floating CMOS input sits near its threshold and draws crossbar current.

**`g_millis` does not advance across a sleep.** SysTick is clocked from HCLK,
which stops. Anything needing real elapsed time uses `power_rtc_ticks()`,
which comes from the RTC domain and keeps running — that is also how the wake
message reports how long it was out.

The loop only considers sleeping once no pad is down, no gesture is
mid-decode, no animation is running, no advertising burst is outstanding,
USB is out and the flashlight is off, plus `FOB_IDLE_LINGER_MS` of slack so
a user between taps of a double-press is never cut off.

## Next revision

The pad LEDs move from PB7 to **PA14** so they can be driven by SPI+DMA, with
the CON1 header taking PB7 in exchange. Reasoning, the two details that will
bite if they are not carried across, and a way to validate the whole path on
the *current* board with one jumper:
[`docs/next-revision.md`](docs/next-revision.md).

## Splitting an LED glitch from the radio

A pad LED glitch that lands on the same beat as a gesture has two very
different possible causes, and they are hard to tell apart by looking:

- the **WS2812 waveform** — see the `T0H` margin table above; the tell is
  that it lands on pixel 0, the only pixel fed the raw signal from PB7
- the **radio** — a BLE burst pulls ~18 mA in bursts off the same 3V3 rail,
  and iSLER runs its own interrupt handler. A gesture starts an animation
  and a burst in the same millisecond, so the two are perfectly correlated
  by construction

One flash separates them:

```bash
make clean && make RADIO=0 flash
```

Buttons, gestures and LEDs behave exactly as normal; nothing transmits and
the radio is never initialised (the boot banner says so). If the glitch
survives, it is the waveform and the bit timing is where to look. If it
vanishes, it is supply or interrupt coupling from the transmit and none of
the bit timing is implicated.

## Tests

`tests/` compiles the target's own sources natively against a stub
`ch32fun.h` (registers become variables) and a link-time I²C fake, so the
firmware is checked by running it rather than by reading it. Nothing is
reimplemented, except the AES block and DataFlash, which get a software AES
and a RAM fake that can cut power mid-write. Fifteen suites:

```bash
make -C tests
```

| Suite | Covers |
|---|---|
| `test_bthome` | plaintext and encrypted frame layout, 31-byte limit, sealed frames decrypted back, address handling |
| `test_buttons` | debounce and every gesture, pad independence, the PB22/PB23 remap |
| `test_leds` | layering, press glow, on-air suppression, charge layer, brightness ceiling |
| `test_power` | the sleep/wake register setup: every wake-source bit armed, pins quiesced |
| `test_charger` | the four AW32001 corrections in order, read-back, repair on revert |
| `test_chirp` | when the status chirp is due |
| `test_status` | what the chirp says for each charger state |
| `test_battery` | supply reading and the battery-low hysteresis |
| `test_temp` | die temperature against the factory calibration word |
| `test_flashlight` | duty cap, momentary, latch, dimmer, timeout, low-battery refusal |
| `test_isp` | the SW1-hold bootloader gesture and its USB check |
| `test_ccm` | AES-CCM against FIPS-197 and BTHome's published encrypted frame, tamper and replay rejection |
| `test_keystore` | key and counter journal: 500 random power cuts mid-write, corrupt records, erased flash, wear, the 2^32 ceiling |
| `test_entropy` | key conditioning and the stuck-source tripwire |
| `test_keyhold` | SW2 export / SW2+SW3 rekey holds and their USB rules; the typed key sequence and NumLock handling |

The three suites below are described in more detail because they are the
ones most likely to need extending.

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
  ws2812.c/h     bit-banged WS2812B-V5/V6 on PB7, runs from RAM
  led_anim.c/h   per-pixel layering: gesture, on-air, press
  bthome.c/h     BTHome v2 packet builder
  ble_adv.c/h    iSLER advertising bursts
  power.c/h      DC-DC, RTC timebase, pin quiescing, deep sleep
  charger.c/h    AW32001: status, and the four defaults it must correct
  i2c.c/h        polled single-byte I2C master, replacing ch32fun's lib
  battery.c/h    supply reading and the battery-low decision
  temp.c/h       die temperature from the chip's own sensor
  flashlight.c/h white LEDs on PWM6+PWM8, interleaved: momentary, latch, SW2/SW3 dimmer, duty cap
  status.c/h     what the status chirp says
  chirp.c/h      when the status chirp is due
  isp.c/h        SW1-hold into the ROM bootloader
  aes.h          one AES-128 block; aes_hw.c drives the CH592's AES peripheral
  ccm.c/h        AES-CCM as BTHome uses it, plus the CBC-MAC conditioner
  keystore.c/h   bindkey, replay-counter reservation, exposure count journal
  dataflash.c/h  DataFlash read / program / page erase, from RAM
  entropy.c/h    noise pool and key generation
  keyhold.c/h    SW2 key-export and SW2+SW3 rekey holds
  hidtype.c/h    the key as a sequence of keyboard reports
  usb_hid.c/h    USB keyboard on ch32fun's fsusb; usb_config.h has its descriptors
  libc_shim.c    one strong memset; see the file for why
  include/       freestanding libc headers for Homebrew's GCC
tests/           host-side test suites and the ch32fun / I2C / AES / DataFlash stubs
docs/            charger power path, I2C pull-ups, power budget, next revision
tools/           Linux udev rule for the WCH ISP bootloader
```

## License

MIT, see [LICENSE](LICENSE). The vendored [ch32fun](ch32fun/) is MIT as well.
