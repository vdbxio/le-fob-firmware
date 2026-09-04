#ifndef LE_FOB_BOARD_H
#define LE_FOB_BOARD_H

/*
 * Le'Fob hardware description — WCH CH592X, QFN-32.
 *
 * Every pin below was read off the CH592 datasheet pin table (CH592DS1 V1.9,
 * section 1.2) using the *CH592X* column, cross-referenced against the board
 * netlist. QFN pad number -> net name -> CH592 GPIO:
 *
 *   pad  2  PWM8    PB6    IR emitter gate  (U1, DNP on this build)
 *   pad  3  PWM6    PB0    white LED gate   (LED1/LED2 via Q1)
 *   pad 10  BTN4    PA9    SW4
 *   pad 11  SCL     PB15   I2C (OLED + AW32001 charger)
 *   pad 12  SDA     PB14   I2C
 *   pad 17  PIXEL0  PB7    WS2812 chain W1->W2->W3->W4   <-- "LED0" net
 *   pad 18  BTN2    PB4    SW2
 *   pad 19  BTN3    PB23   SW3, doubles as RST#
 *   pad 20  BOOT    PB22   SW1, doubles as ROM-bootloader probe
 *   pad 29  PIXEL1  PA14   external pixel header CON1 (unused here)
 *
 * All four switches are wired to GND, so they are active-low and need the
 * internal pull-up.
 *
 * Two of the four pads carry a second, chip-level function:
 *
 *   SW1 / PB22 is the ROM bootloader probe. Holding it while USB is plugged
 *   in enters ISP mode instead of running this firmware. That is the intended
 *   flashing gesture; in normal operation PB22 is just button 1.
 *
 *   SW3 / PB23 is RST# out of the factory: pressing it resets the chip rather
 *   than registering a button. It becomes a usable GPIO only after the
 *   CFG_RESET_EN option bit is cleared once per board ("make disable-reset").
 */

#include "ch32fun.h"

/* ------------------------------------------------------------------ */
/* Buttons — active low, internal pull-up                             */
/* ------------------------------------------------------------------ */
#define NUM_BUTTONS 4

#define PIN_BTN1  PB22   /* SW1 — also BOOT   */
#define PIN_BTN2  PB4    /* SW2               */
#define PIN_BTN3  PB23   /* SW3 — also RST#   */
#define PIN_BTN4  PA9    /* SW4               */

/* ------------------------------------------------------------------ */
/* WS2812 chain on the PIXEL0 / "LED0" net                            */
/* ------------------------------------------------------------------ */
/*
 * PB7 is not an SPI MOSI pin, so ch32fun's SPI+DMA WS2812 driver (which
 * requires PA14) cannot drive this chain. The next board revision swaps
 * this net with PIXEL1 so the pads land on PA14 and can be DMA-driven --
 * see docs/next-revision.md, which also records that ch32fun's stock SPI
 * encoding is not V5-compliant and needs two small changes first. PA14 on this board is PIXEL1,
 * which only goes to the external CON1 header. Hence the bit-bang in
 * ws2812.c — four pixels is ~120 us per refresh, which is affordable.
 */
#define PIN_WS2812   PB7
#define WS2812_COUNT 4

/* Chain order W1..W4 matches buttons 1..4. */

/*
 * Global ceiling on pad LED brightness, 0-100. Every colour any layer
 * produces is scaled by this, so it is the one knob for "how bright is the
 * fob". These sit ~10 mm from the user's eye in a dark pocket; full scale is
 * unpleasant and wastes battery.
 */
#ifndef PAD_LED_BRIGHTNESS_PCT
#define PAD_LED_BRIGHTNESS_PCT 10
#endif

/*
 * The pads are dark at rest. Three things light them, in priority order --
 * see led_anim.c:
 *
 *   1. a decoded gesture   distinct pattern per gesture, identifies what the
 *                          firmware thinks you did
 *   2. an advertising burst  the fob is on air
 *   3. a pad physically held  immediate feedback, before any gesture has been
 *                          decoded
 *
 * Levels below are percentages *of* PAD_LED_BRIGHTNESS_PCT, not of full
 * scale, so raising or lowering the global ceiling moves all of them
 * together. Gesture patterns run at the full ceiling.
 */

/* Steady glow while a pad is held down. */
#ifndef PAD_LED_PRESS_PCT
#define PAD_LED_PRESS_PCT 35
#endif

/* Glow while the radio is transmitting a burst. */
#ifndef PAD_LED_TX_PCT
#define PAD_LED_TX_PCT 55
#endif

/* Set either to 0 to drop that layer entirely. */
#ifndef PAD_LED_SHOW_PRESS
#define PAD_LED_SHOW_PRESS 1
#endif

#ifndef PAD_LED_SHOW_TX
#define PAD_LED_SHOW_TX 1
#endif

/* ------------------------------------------------------------------ */
/* Gesture timing                                                     */
/* ------------------------------------------------------------------ */

/* Contact settling time before a level change is believed (ms). */
#ifndef BTN_DEBOUNCE_MS
#define BTN_DEBOUNCE_MS 8
#endif

/* How long to wait after a release for another tap before deciding
 * whether it was a single / double / triple press (ms). This is also
 * the latency floor for a single press, so keep it tight. */
#ifndef BTN_MULTITAP_MS
#define BTN_MULTITAP_MS 280
#endif

/* Held this long -> long_press fires immediately, without waiting for
 * the release (ms). */
#ifndef BTN_LONG_MS
#define BTN_LONG_MS 600
#endif

/* Held this long -> hold_press fires, again without waiting (ms). */
#ifndef BTN_HOLD_MS
#define BTN_HOLD_MS 1500
#endif

/* ------------------------------------------------------------------ */
/* AW32001 battery charger, on the shared I2C bus                     */
/* ------------------------------------------------------------------ */
#define PIN_I2C_SDA PB14   /* pad 12 */
#define PIN_I2C_SCL PB15   /* pad 11 */
#define PIN_CHG_INT PA4    /* pad 26, charger INT (open drain, active low) */

#define CHARGER_I2C_ADDR 0x49

/* The board fits pull-ups (R3/R4); 100 kHz is comfortable. Drop this if
 * running on internal pull-ups instead -- see docs/i2c-pullups.md. */
#ifndef CHARGER_I2C_KHZ
#define CHARGER_I2C_KHZ 100
#endif

/*
 * The charger driver is OFF by default.
 *
 * It is not proven on hardware, and its failure mode is severe rather than
 * inconvenient: it read-modify-writes REG06, one bit of which is FET_DIS,
 * and setting that puts the part into shipping mode -- from which this board
 * cannot recover without USB. The I2C library it sits on can silently
 * corrupt a received byte (it re-reads DATAR when BTF is set, overwriting
 * the value), so a bad read becomes a bad write.
 *
 * charger.c now verifies every write and reverts the part to default mode if
 * it cannot, but until that has actually been exercised on a board the
 * conservative default is not to run it at all. Enable deliberately:
 *
 *     make clean && make EXTRA_CFLAGS="-ffreestanding -Iinclude \
 *         -I../ch32fun/examples_ch5xx/i2c_sensors -DFOB_ENABLE_CHARGER=1"
 */
#ifndef FOB_ENABLE_CHARGER
#define FOB_ENABLE_CHARGER 0
#endif

/* ------------------------------------------------------------------ */
/* Factory info block                                                 */
/* ------------------------------------------------------------------ */
/*
 * Address of the factory-programmed BLE address inside InfoFlash.
 *
 * The datasheet (table 2-2) puts InfoFlash at 0x0007E000-0x0007FFFF and
 * reserves 0x0007E000-0x0007EEFF for user configuration, leaving the block
 * above that to WCH. Section 4.5 says a unique per-chip ID lives in a
 * read-only area but defers the address to "the routines" -- and the
 * routine in question, GetMACAddress(), only exists inside WCH's closed
 * BLE blob, so there is no first-party constant to quote.
 *
 * What can be checked: ch32fun places the same block at 0x0003F018 on the
 * 256 KB CH570/CH572. That is 0x40000 - 0xFE8, i.e. a fixed offset back
 * from the top of flash, and applying the same offset to this part's
 * 512 KB gives 0x80000 - 0xFE8 = 0x0007F018 -- which is also the address
 * CH58x code in the wild uses. Consistent, but inferred rather than
 * documented, so bthome.c validates what it reads against WCH's OUI and
 * falls back to a synthesised address if it does not look like one.
 */
/* Overridable so the host-side packet test can point it at a buffer. */
#ifndef LEFOB_ROM_MAC_ADDR
#define LEFOB_ROM_MAC_ADDR ((const uint8_t *)0x0007F018)
#endif

/* ------------------------------------------------------------------ */
/* BLE / BTHome                                                       */
/* ------------------------------------------------------------------ */

/* Name advertised in the "Complete Local Name" AD structure. This is what
 * Home Assistant shows when it discovers the fob. */
#define BTHOME_NAME "le-fob"

#define BLE_TX_POWER LL_TX_POWER_0_DBM

/*
 * A BTHome event is a one-shot: there is no state for a receiver to catch up
 * on later, so a dropped advertisement is a dropped button press. We send the
 * same packet ID this many times across all three advertising channels to
 * make that unlikely. Receivers de-duplicate on the packet ID.
 */
#ifndef ADV_BURST_ROUNDS
#define ADV_BURST_ROUNDS 12
#endif

/* Gap between rounds of the burst (ms). */
#ifndef ADV_ROUND_INTERVAL_MS
#define ADV_ROUND_INTERVAL_MS 20
#endif

/*
 * Rounds for the periodic status chirp. Fewer than a button event: a
 * missed chirp is repaired by the next one, whereas a missed press is
 * simply gone. Each round is one wake-cycle's worth of radio, so this
 * trades reliability of a single chirp against battery directly.
 */
#ifndef ADV_HEARTBEAT_ROUNDS
#define ADV_HEARTBEAT_ROUNDS 4
#endif

/* ------------------------------------------------------------------ */
/* Status chirp                                                        */
/* ------------------------------------------------------------------ */
/*
 * How often the fob wakes on its own to advertise, with no button pressed.
 *
 * Measured 2026-09-04: a chirp is ~100 ms awake and ~0.13 uAh -- about
 * fifteen times cheaper than a button press, because there is no LED
 * animation and only four burst rounds. Against a 28.5 uA floor on the
 * 120 mAh cell:
 *
 *      interval   chirps/day   uAh/day   days
 *        30 s        2880        372      163   (floor alone is 684)
 *        60 s        1440        186      138
 *         5 min       288         37      166
 *        15 min        96         12      172
 *        never          0          0      175
 *
 * So even a 60 s chirp costs a fifth of the lifetime, and 5 min costs
 * five percent. The interval is a product choice about how stale a
 * status may be, not a battery one.
 *
 * The intended production value is 5 minutes: Home Assistant's "last seen"
 * stays fresh enough to trust, at ~two thirds of the no-chirp lifetime.
 * It is 1 minute for now so that development captures need not run for
 * a quarter of an hour to contain a couple of chirps. Set to 0 to disable
 * chirping entirely.
 */
#ifndef CHIRP_INTERVAL_MS
#define CHIRP_INTERVAL_MS (60u * 1000u)
#endif

/*
 * The AW32001's INT pin (PA4) fires on plug, unplug, charge-state change
 * and faults. Each one sends an interim chirp immediately, so Home
 * Assistant learns about USB going in or out without waiting for the
 * next scheduled one. An interim chirp also restarts the schedule.
 *
 * INT can chatter -- the charge state machine can step through several
 * states in quick succession -- so interim chirps are rate-limited. Any
 * INT inside this window is folded into the chirp already in flight.
 */
#ifndef CHARGER_INT_MIN_GAP_MS
#define CHARGER_INT_MIN_GAP_MS 2000
#endif

/*
 * How long to stay awake after a chirp before sleeping again. Button wakes
 * use FOB_IDLE_LINGER_MS, which is sized for a human between taps; a chirp
 * wake has no human in it and only needs the burst to finish.
 */
#ifndef FOB_CHIRP_LINGER_MS
#define FOB_CHIRP_LINGER_MS 30
#endif

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */
/*
 * Build the LED path without the radio:
 *
 *     make clean && make EXTRA_CFLAGS="-ffreestanding -Iinclude -DFOB_DISABLE_RADIO=1"
 *
 * Buttons, gestures and pad LEDs behave exactly as normal; nothing is
 * transmitted and the radio is never initialised. This exists to split a
 * class of fault that is otherwise hard to pin down from the outside: an
 * LED glitch that lands on the same beat as a gesture could be the WS2812
 * signal itself, or it could be the radio -- a BLE burst pulls ~18 mA in
 * bursts off the same 3V3 rail, and iSLER runs its own interrupt handler.
 *
 * If the glitch survives with the radio out of the picture, it is the
 * WS2812 waveform (see the T0H margin table in ws2812.c). If it vanishes,
 * it is supply or interrupt coupling from the transmit, and none of the bit
 * timing is implicated.
 */
#ifndef FOB_DISABLE_RADIO
#define FOB_DISABLE_RADIO 0
#endif

/*
 * Build without the internal pull-ups that power.c puts on otherwise-unused
 * pins:
 *
 *     make clean && make EXTRA_CFLAGS="-ffreestanding -Iinclude \
 *         -DFOB_SLEEP_ENABLE=1 -DFOB_QUIESCE_PULLUPS=0"
 *
 * A pull-up is the right thing for an input that would otherwise float, but
 * it is exactly the wrong thing for one that something else holds low: the
 * CH592's internal pull-up is in the tens of kilohms, so it would source
 * 65-165 uA continuously. PA4 is the one to suspect -- it is the AW32001's
 * INT, which is open-drain and low while asserted.
 *
 * Building with this at 0 leaves those pins floating, which is not a state
 * to ship, but it separates "my pull-ups are sourcing current" from "the
 * board draws this regardless" in one measurement.
 */
#ifndef FOB_QUIESCE_PULLUPS
#define FOB_QUIESCE_PULLUPS 1
#endif

/* ------------------------------------------------------------------ */
/* Sleep                                                              */
/* ------------------------------------------------------------------ */
/*
 * Deep sleep, on by default.
 *
 * It was off through bring-up, on the grounds that a chip which drops into
 * deep sleep is harder to talk to. It has since been measured working -- a
 * flat 257 uA floor held for 12.8 s with no wakes -- and the difference it
 * makes is the difference between a fob that lasts a day and one that lasts
 * months, so it is now what a plain `make flash` gives you.
 *
 * Set to 0 for bring-up work where you would rather the chip stayed awake
 * and responsive:
 *
 *     make clean && make EXTRA_CFLAGS="-ffreestanding -Iinclude -DFOB_SLEEP_ENABLE=0"
 */
#ifndef FOB_SLEEP_ENABLE
#define FOB_SLEEP_ENABLE 1
#endif

/* Stay awake this long after the last interesting thing happened, so a
 * user mid-gesture is never cut off (ms). */
/*
 * Settling time after waking, before any timing-critical work. WCH uses
 * 20 us in their own sleep routine; this is deliberately generous, because
 * it is paid once per wake and the failure it prevents is silent corruption
 * of both the LED waveform and the radio.
 */
#ifndef FOB_WAKE_SETTLE_US
#define FOB_WAKE_SETTLE_US 250
#endif

#ifndef FOB_IDLE_LINGER_MS
#define FOB_IDLE_LINGER_MS 400
#endif

#endif /* LE_FOB_BOARD_H */
