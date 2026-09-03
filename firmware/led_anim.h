#ifndef LE_FOB_LED_ANIM_H
#define LE_FOB_LED_ANIM_H

#include <stdint.h>

/*
 * Non-blocking per-pixel animation over the four WS2812s on the PIXEL0
 * chain. Each pixel is tied to the button beneath it, and each gesture gets
 * a visually distinct pattern so the fob is usable without looking at Home
 * Assistant to find out what it thought you did.
 */

void led_anim_init(void);

/* Start the animation matching `event` (a BTHOME_EVT_* code) on the pixel
 * belonging to `button` (0..WS2812_COUNT-1). Replaces whatever that pixel
 * was doing; other pixels are untouched. */
void led_anim_trigger(uint8_t button, uint8_t event, uint32_t now);

/* Recompute and, if anything changed, push a frame. Cheap to call every
 * millisecond; it rate-limits its own refreshes internally. */
void led_anim_tick(uint32_t now);

/* Nonzero while any pixel is still animating. */
int led_anim_busy(void);

/* Blank the chain immediately (used before sleeping). */
void led_anim_all_off(void);

#endif /* LE_FOB_LED_ANIM_H */
