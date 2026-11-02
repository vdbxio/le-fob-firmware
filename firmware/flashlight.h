#ifndef LE_FOB_FLASHLIGHT_H
#define LE_FOB_FLASHLIGHT_H

#include <stdint.h>

/*
 * The two white LEDs behind Q1, one per FET channel: PWM6 on PB0 drives
 * Q1-B, PWM8 on PB6 drives Q1-A. The two are run interleaved, so up to 50%
 * duty the peak current is one LED's worth, not two.
 *
 * There is no current limiting on this board revision: LED1 and LED2 sit
 * straight across the system rail through the FETs, and the only things
 * holding the current down are the LEDs' own resistance, the FET and the
 * cell's internal resistance. PWM duty is therefore the current limiter,
 * which is why FLASHLIGHT_MAX_PCT is a hard cap and nothing here can
 * exceed it.
 *
 * Behaviour, on the flashlight pad:
 *
 *   press and hold    light while held at FLASHLIGHT_PCT, off on release
 *   double tap        latched on at FLASHLIGHT_PCT; any press turns it off
 *   latched too long  FLASHLIGHT_LATCH_MAX_MS, then off by itself
 *
 * While latched the dim pad (SW2) and bright pad (SW3) step the level by
 * FLASHLIGHT_STEP_PCT between FLASHLIGHT_STEP_PCT and FLASHLIGHT_MAX_PCT.
 * Those presses belong to the torch: flashlight_took_press() tells the
 * main loop to keep their BTHome events off the air. Each new latch starts
 * back at FLASHLIGHT_PCT, so a hacker who left it at the cap does not get
 * the cap by surprise next time.
 *
 * The light is refused while the battery reads low: a light-flattened fob
 * must keep enough charge to advertise and to shut down cleanly.
 */

void flashlight_init(void);

/*
 * Feed every tick with the current pressed mask. Handles the momentary
 * behaviour, the latch timeout and the brightness pads. `batt_low` nonzero
 * refuses to light.
 */
void flashlight_tick(uint8_t pressed_mask, int batt_low, uint32_t now);

/* A double tap on the flashlight pad was decoded: latch on. */
void flashlight_latch(uint32_t now);

/*
 * Test-and-clear: was this pad's last press taken as a brightness step?
 * The main loop asks when it pops the pad's decoded event, and drops the
 * event if so.
 */
int flashlight_took_press(uint8_t button);

/* Nonzero while the LEDs are being driven. The fob must not deep-sleep
 * with this set: the PWM stops and the gate is left wherever it was. */
int flashlight_on(void);

/* Current drive level in percent, 0 when off. For the log. */
uint8_t flashlight_level_pct(void);

#endif /* LE_FOB_FLASHLIGHT_H */
