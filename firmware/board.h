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

/*
 * Pad 1 doubles as the charge indicator while USB is in: magenta breathing
 * while the cell charges, steady green when it is full, slow red blink on
 * a charger fault. Lowest priority of all the layers, so a press or a
 * gesture on pad 1 shows over it. The fob stays awake while on USB to
 * animate it -- it is being powered from the cable, so that costs nothing.
 *
 *     make LED_CHARGE=0 flash     to drop it
 */
#ifndef PAD_LED_SHOW_CHARGE
#define PAD_LED_SHOW_CHARGE 1
#endif

/* Peak level of the charge indicator, as a percentage of the ceiling. */
#ifndef PAD_LED_CHARGE_PCT
#define PAD_LED_CHARGE_PCT 60
#endif

#ifndef PAD_LED_SHOW_TX
#define PAD_LED_SHOW_TX 1
#endif

/* ------------------------------------------------------------------ */
/* Flashlight -- LED1/LED2 behind Q1, gates on PWM6 + PWM8             */
/* ------------------------------------------------------------------ */
#define PIN_FLASHLIGHT_A PB0   /* pad 3, PWM6 -> R9 -> Q1-B gate -> one white LED */
#define PIN_FLASHLIGHT_B PB6   /* pad 2, PWM8 -> R7 -> Q1-A gate -> the other, or the IR */

/*
 * Which FET channels the torch drives: bit 0 = PWM6/Q1-B, bit 1 = PWM8/Q1-A.
 * Both carry a white LED as built. The IR emitter footprint shares Q1-A's
 * pad, so a board reworked for IR runs the torch on PWM6 alone:
 *
 *     make FLASHLIGHT_CHANNELS=1 flash
 */
#ifndef FLASHLIGHT_CHANNELS
#define FLASHLIGHT_CHANNELS 3
#endif

/* Which pad is the flashlight: index 3 = SW4, furthest from the USB port
 * on this board revision. The production board flips the numbering. */
#ifndef FLASHLIGHT_BUTTON
#define FLASHLIGHT_BUTTON 3
#endif

/*
 * PWM duty, and therefore the only current limit the white LEDs have on
 * this revision (see flashlight.h).
 *
 * FLASHLIGHT_PCT is the momentary level and the level every latch starts
 * at. 20% is the handout setting: bright enough to be a torch, well under
 * what the cell and the 3.8 x 1.0 mm LED packages will tolerate for a
 * sustained on.
 *
 * While latched, SW2 steps the light down and SW3 up, FLASHLIGHT_STEP_PCT
 * at a time, between FLASHLIGHT_STEP_PCT and FLASHLIGHT_MAX_PCT. The
 * maximum is the hard cap: nothing in the firmware drives the LEDs harder.
 * 100% has not been smoke-tested on these LEDs; a bench run that wants it
 * raises the cap knowingly:
 *
 *     make FLASHLIGHT_MAX_PCT=100 flash
 */
#ifndef FLASHLIGHT_PCT
#define FLASHLIGHT_PCT 20
#endif
#ifndef FLASHLIGHT_MAX_PCT
#define FLASHLIGHT_MAX_PCT 90
#endif
#ifndef FLASHLIGHT_STEP_PCT
#define FLASHLIGHT_STEP_PCT 10
#endif

/* The brightness pads while latched: index 1 = SW2 down, index 2 = SW3 up. */
#ifndef FLASHLIGHT_DIM_BUTTON
#define FLASHLIGHT_DIM_BUTTON 1
#endif
#ifndef FLASHLIGHT_BRIGHT_BUTTON
#define FLASHLIGHT_BRIGHT_BUTTON 2
#endif

/* A double-press decode arriving this soon after a press switched the
 * latch off is the tail of that same "off" gesture, not a new "on". Longer
 * than two taps plus the multi-tap window. */
#ifndef FLASHLIGHT_RELATCH_GUARD_MS
#define FLASHLIGHT_RELATCH_GUARD_MS 1000
#endif

/* A latched light forgotten in a bag flattens the cell; cut it off. */
#ifndef FLASHLIGHT_LATCH_MAX_MS
#define FLASHLIGHT_LATCH_MAX_MS (5u * 60u * 1000u)
#endif

#ifndef FOB_ENABLE_FLASHLIGHT
#define FOB_ENABLE_FLASHLIGHT 1
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
/* Bootloader entry from the running app (see isp.h)                  */
/* ------------------------------------------------------------------ */

/*
 * Hold SW1 alone this long and the app jumps to the ROM bootloader. Far
 * past the hold_press gesture, so a deliberate hold is unmistakable (ms).
 */
#ifndef ISP_HOLD_MS
#define ISP_HOLD_MS 5000
#endif

/* From here until ISP_HOLD_MS the pads fill in one by one as a warning:
 * let go and nothing happens (ms). */
#ifndef ISP_ARM_MS
#define ISP_ARM_MS 2000
#endif

/*
 * Only jump if the charger reports VBUS present. The bootloader is useless
 * without USB and, once in it, the only way out is a reset -- which on a
 * battery-powered board means pulling the battery. When the charger driver
 * is off or not answering the check is skipped, so you can never be locked
 * out of flashing by a dead charger.
 */
#ifndef FOB_ISP_REQUIRE_USB
#define FOB_ISP_REQUIRE_USB 1
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
 * The charger driver, on by default.
 *
 * It was off while it was unproven, because its original failure mode was
 * severe: a blind read-modify-write of REG06 over an I2C library that can
 * silently corrupt a byte, with bit 5 of that register being FET_DIS --
 * shipping mode, from which this board cannot recover without USB.
 *
 * That is no longer the shape of it. Every write is verified by reading
 * back, FET_DIS is force-cleared on every REG06 write rather than
 * preserved, a failed configuration resets the part to default mode, and
 * a bus that does not answer results in no writes at all. The failure
 * mode is now a word in the status text ("chgr nak", "chgr id?",
 * "chgr cfg!") instead of a dead board -- which, on a board with no
 * console, is the only diagnostic channel there is anyway.
 *
 *     make CHARGER=0 flash        to build without it
 */
#ifndef FOB_ENABLE_CHARGER
#define FOB_ENABLE_CHARGER 1
#endif

/*
 * Fast-charge current, mA. The AW32001 steps in 8 mA from 8 to 512; its
 * reset value is 128 mA, about 1.3C on the 100 mAh cell. 48 mA is ~0.5C,
 * the gentle end of the usual LiPo range: a flat cell is full in about
 * three hours, well inside the charger's 5 h safety timer. Raise it for a
 * bigger cell:
 *
 *     make CHG_MA=96 flash
 */
#ifndef CHARGER_ICHG_MA
#define CHARGER_ICHG_MA 48
#endif

_Static_assert(CHARGER_ICHG_MA >= 8 && CHARGER_ICHG_MA <= 512 &&
               CHARGER_ICHG_MA % 8 == 0,
               "CHARGER_ICHG_MA must be a multiple of 8 between 8 and 512");

/*
 * Battery low, from the chip's own supply (see battery.h). Low once the
 * supply sits BATT_LOW_DROP_MV under the highest reading seen -- the LDO
 * has fallen out of regulation and the cell is in its last few percent --
 * or at BATT_LOW_FLOOR_MV regardless. Clears within BATT_LOW_CLEAR_MV of
 * the reference, or on USB.
 */
#ifndef BATT_LOW_DROP_MV
#define BATT_LOW_DROP_MV 60
#endif
#ifndef BATT_LOW_CLEAR_MV
#define BATT_LOW_CLEAR_MV 20
#endif
#ifndef BATT_LOW_FLOOR_MV
#define BATT_LOW_FLOOR_MV 3150
#endif

/*
 * While USB is present the fob stays awake and re-reads the charger this
 * often, so the pad 1 indicator and the "charging" state follow a plug or
 * unplug within a second even on boards where the INT jumper is not fitted.
 * Off USB nothing polls; the chirp and the INT are the only readers.
 */
#ifndef CHARGER_POLL_MS
#define CHARGER_POLL_MS 1000
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

/*
 * Temperature sensor calibration word, ROM_CFG_TMP_25C in WCH's
 * CH59x_adc.h: four bytes below the address block above. Overridable for
 * the host test, like the MAC.
 */
#ifndef LEFOB_ROM_TMP25C_ADDR
#define LEFOB_ROM_TMP25C_ADDR ((const volatile uint32_t *)0x0007F014)
#endif

/* ------------------------------------------------------------------ */
/* BLE / BTHome                                                       */
/* ------------------------------------------------------------------ */

/* Name advertised in the "Complete Local Name" AD structure. This is what
 * Home Assistant shows when it discovers the fob. */
#define BTHOME_NAME "le-fob"

/*
 * Transmit power. The CH592 radio goes from -25 up to +7 dBm; iSLER's
 * LL_TX_POWER_* table in ch5xxhw.h has the register values. The cost is
 * only paid on air -- a 12-round burst is ~15 ms of it -- so even the top
 * setting is invisible next to the sleep floor. Override with
 *
 *     make TXPOWER=7 flash      (or TXPOWER=-5, etc.)
 *
 * BLE_TX_POWER_DBM is the same number in human units, for the banner.
 */
#ifndef BLE_TX_POWER
#define BLE_TX_POWER     LL_TX_POWER_7_DBM /* measured: 96% -> 99% over +6 */
#define BLE_TX_POWER_DBM 7
#endif
#ifndef BLE_TX_POWER_DBM
#define BLE_TX_POWER_DBM 0 /* unknown: BLE_TX_POWER was set by hand */
#endif

/*
 * A BTHome event is a one-shot: there is no state for a receiver to catch up
 * on later, so a dropped advertisement is a dropped button press. We send the
 * same packet ID this many times across all three advertising channels to
 * make that unlikely. Receivers de-duplicate on the packet ID.
 *
 * Measured (2 s chirps, sleep off, Home Assistant packet-id history;
 * receivers are ESPHome proxies at default scan settings plus the HA
 * host's own BlueZ adapter, all within a few feet):
 *
 *     12 rounds over  240 ms     31-36% delivered
 *     60 rounds over 1200 ms     96-99% delivered
 *     12 rounds over 1200 ms     fine
 *
 * Misses at 240 ms came in runs of up to 14 consecutive bursts. A scanner
 * listens in windows, and a burst that fits inside one scan interval
 * either straddles a window or loses every repeat together. Spanning
 * several intervals gives each its own chance. This is why real BTHome
 * buttons advertise for a second or more.
 *
 * The third row says the gap between repeats is not the constraint at
 * these receivers: 100 ms apart still lands. 20 is a middle setting, 60 ms
 * apart, chosen by the owner; it leaves margin without paying for 60.
 *
 * Cost: airtime itself is trivial (~1 ms per round). Until the chip sleeps
 * between rounds the price is being awake for ADV_BURST_MS per burst, and
 * once it does, the price becomes per-round -- which is when this number
 * starts to matter for battery; see docs/power-budget.md.
 */
#ifndef ADV_BURST_ROUNDS
#define ADV_BURST_ROUNDS 20
#endif

/*
 * How long the whole burst is on air (ms). The rounds are spread evenly
 * across it, so the gap between repeats is derived, not set: 60 rounds
 * over 1200 ms is one every 20 ms; 12 over 1200 ms is one every 100 ms.
 *
 * Two different receivers care about the two numbers. The span has to
 * cover several of the scanner's intervals (that is the 36% -> 98% fix
 * above). The gap has to be shorter than the scanner's listening window,
 * or a window can open and close between two repeats -- so thinning the
 * burst is safe only up to whatever window your proxies use. Test before
 * trusting a longer gap:
 *
 *     make ROUNDS=12 BURST_MS=1200 SLEEP=0 CHIRP_MS=2000 flash
 */
#ifndef ADV_BURST_MS
#define ADV_BURST_MS 1200
#endif

/* Derived: gap between rounds (ms). Not a knob. */
#define ADV_ROUND_INTERVAL_MS (ADV_BURST_MS / ADV_BURST_ROUNDS)

_Static_assert(ADV_BURST_ROUNDS >= 1, "ADV_BURST_ROUNDS must be at least 1");
_Static_assert(ADV_ROUND_INTERVAL_MS >= 2,
               "ADV_BURST_MS / ADV_BURST_ROUNDS is under 2 ms: a round is ~1 ms of airtime");

/*
 * 32 MHz crystal load capacitance, in pF, applied through the chip's
 * internal tuning caps (R8_XT32M_TUNE). 0 leaves the reset value alone.
 *
 * Why it is here: the carrier is derived from this crystal, and a load
 * mismatch pulls it off frequency in proportion. BLE receivers tolerate
 * roughly +/-150 kHz, which at 2.4 GHz is ~60 ppm; a crystal seeing the
 * wrong load can miss by that much, and the symptom is exactly "some
 * packets decode, most do not" with no firmware change making a
 * difference. Sweep it with the 2 s chirp test and watch delivery:
 *
 *     make HSE_CAP=12 SLEEP=0 CHIRP_MS=2000 flash
 *
 * Even values 10..24 only (the register steps in 2 pF from 10).
 *
 * Status: the board has no external caps on either crystal, so the internal
 * trim is the whole load and the reset value is what WCH intend. The
 * delivery problem turned out to be burst length, not frequency (see
 * ADV_BURST_ROUNDS); this knob stays for the day a board disagrees.
 */
#ifndef FOB_HSE_CAP_PF
#define FOB_HSE_CAP_PF 0
#endif

/*
 * Rounds for the periodic status chirp. Matches the button burst.
 *
 * It was 4, on the reasoning that a missed chirp is repaired by the next
 * one. That was wrong for the reason above: a short burst either straddles
 * a scan window or it does not, and at 4 rounds it mostly did not (about
 * one in six landed). The chirp is the availability signal and, unlike a
 * press, cannot be retried by a human, so it gets the same burst a press
 * does. At a 5 min production interval, three misses in a row -- what it
 * takes to go "unavailable" -- is now a once-in-years event rather than a
 * daily one.
 */
#ifndef ADV_HEARTBEAT_ROUNDS
#define ADV_HEARTBEAT_ROUNDS ADV_BURST_ROUNDS
#endif

/* ------------------------------------------------------------------ */
/* Status chirp                                                        */
/* ------------------------------------------------------------------ */
/*
 * How often the fob wakes on its own to advertise, with no button pressed.
 *
 * Measured 2026-09-04 at 4 heartbeat rounds: ~100 ms awake, ~0.13 uAh.
 * The chirp now uses the full ADV_BURST_MS burst, which is awake time, so
 * it is projected at ~1.8 uAh until the chip sleeps between rounds (see
 * docs/power-budget.md). The table below is the old 4-round measurement,
 * kept as the floor a sleeping burst should get back to. Against a
 * 28.5 uA floor on the 120 mAh cell:
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
 * 5 minutes: Home Assistant's "last seen" stays fresh enough to trust, at
 * ~two thirds of the no-chirp lifetime. For development captures that
 * should not take a quarter of an hour to contain a couple of chirps:
 *
 *     make CHIRP_MS=60000 flash      (0 disables chirping entirely)
 */
#ifndef CHIRP_INTERVAL_MS
#define CHIRP_INTERVAL_MS (5u * 60u * 1000u)
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
 * Arm PA4 (the charger INT) as a wake source and send an interim chirp on
 * it. Separable from FOB_ENABLE_CHARGER so the two can be tested apart:
 * the I2C/charger bring-up is one thing, adding a second edge-interrupt
 * source on the GPIOA vector is another.
 *
 *     make CHARGER_INT=0 flash
 */
#ifndef FOB_ENABLE_CHARGER_INT
#define FOB_ENABLE_CHARGER_INT 1
#endif

/*
 * Whether the periodic chirp carries a BTHome text object (0x53) with the
 * human-readable charger state, or the plain button-none frame.
 *
 * On by default. The text object is confirmed received -- Home Assistant
 * showed "USB idle" from a chirp -- so the earlier "interval broadcast not
 * landing" was airtime, not the frame, and is addressed by
 * ADV_HEARTBEAT_ROUNDS above. Set to 0 for the plain button-none chirp:
 *
 *     make CHIRP_TEXT=0 flash
 */
#ifndef FOB_CHIRP_TEXT
#define FOB_CHIRP_TEXT 1
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
 *     make clean && make RADIO=0 flash
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
 *     make clean && make PULLUPS=0 flash
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
 *     make clean && make SLEEP=0 flash
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
