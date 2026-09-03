#ifndef LE_FOB_LED_ANIM_H
#define LE_FOB_LED_ANIM_H

#include <stdint.h>

/*
 * Non-blocking per-pixel lighting for the four WS2812s on the PIXEL0 chain.
 * Each pixel is tied to the button beneath it.
 *
 * The pads are dark at rest. Three layers can light one, highest priority
 * first:
 *
 *   1. a decoded gesture -- a distinct pattern per gesture, so the fob says
 *      what it thought you did without opening Home Assistant
 *   2. an advertising burst -- the fob is on air
 *   3. a pad physically held -- immediate feedback, which matters because a
 *      single tap is not decoded until the multi-tap window closes ~280 ms
 *      after release
 *
 * A running gesture animation owns its pixel outright, dark phases included,
 * so the lower layers cannot fight its blink pattern.
 */

void led_anim_init(void);

/* Start the animation matching `event` (a BTHOME_EVT_* code) on the pixel
 * belonging to `button` (0..WS2812_COUNT-1). Replaces whatever that pixel
 * was doing; other pixels are untouched. */
void led_anim_trigger(uint8_t button, uint8_t event, uint32_t now);

/* Pads currently held, as returned by buttons_pressed_mask(). */
void led_anim_set_pressed(uint8_t mask);

/* Pad whose advertising burst is on air, or -1 when the radio is idle. */
void led_anim_set_tx(int button);

/* Recompute and, if anything changed, push a frame. Cheap to call every
 * millisecond; it rate-limits its own refreshes internally. */
void led_anim_tick(uint32_t now);

/* Nonzero while any pixel is still running a gesture animation. The press
 * and TX layers are not counted: they track state owned elsewhere and would
 * otherwise keep the idle check from ever settling. */
int led_anim_busy(void);

/* Blank the chain immediately (used before sleeping). */
void led_anim_all_off(void);

#endif /* LE_FOB_LED_ANIM_H */
