#ifndef LE_FOB_KEYHOLD_H
#define LE_FOB_KEYHOLD_H

#include <stdint.h>

/*
 * The two long holds that handle the bindkey, both only while the fob is
 * enumerated on USB:
 *
 *   SW2 alone,  KEY_EXPORT_HOLD_MS   type the bindkey out as a USB keyboard
 *   SW2 + SW3,  REKEY_HOLD_MS        throw the key away and make a new one
 *
 * SW2 because SW1 already belongs to the bootloader hold (isp.h). Each runs
 * the same pad fuse as the bootloader hold, in its own colour, so an
 * accidental in-pocket hold is visible and can be let go of.
 *
 * Rules, from the MVP spec (section 13):
 *
 *   - the hold must *begin* with USB already enumerated; a pad held down
 *     through plugging in does nothing until it is released
 *   - unplugging mid-hold cancels it
 *   - any other pad joining in cancels it, except SW3 joining SW2, which
 *     turns an export hold into a rekey hold; SW3 first and then SW2 is a
 *     rekey hold too (pressing two pads at exactly the same instant is not
 *     something hands do). The rekey clock starts when both are down
 *   - each fires once per hold; everything must be released before the
 *     next one
 *
 * Timing only, no hardware, so it runs on the host like isp.c.
 */

typedef enum {
	KEYHOLD_NONE = 0,
	KEYHOLD_EXPORT,
	KEYHOLD_REKEY,
} keyhold_event_t;

/* Feed the debounced pad mask every tick. Returns an event on exactly the
 * tick a hold completes. */
keyhold_event_t keyhold_tick(uint8_t pressed_mask, int usb_ready, uint32_t now);

/* 0 while not arming, otherwise 1..254 across the arming window, for the
 * LED fuse; keyhold_arming() says which hold it belongs to. */
uint8_t         keyhold_progress(void);
keyhold_event_t keyhold_arming(void);

#endif /* LE_FOB_KEYHOLD_H */
