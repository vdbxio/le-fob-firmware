/*
 * Key export / rekey hold tracker. See keyhold.h.
 */

#include "keyhold.h"
#include "board.h"

#define SW2_MASK 0x02u
#define SW3_MASK 0x04u

static keyhold_event_t holding;   /* which hold is running, if any */
static uint32_t        t_down;    /* when it started */
static uint8_t         blocked;   /* wait for all-released before anything */
static uint8_t         progress;

static keyhold_event_t want(uint8_t mask)
{
	if (mask == SW2_MASK)
		return KEYHOLD_EXPORT;
	if (mask == (SW2_MASK | SW3_MASK))
		return KEYHOLD_REKEY;
	return KEYHOLD_NONE;
}

keyhold_event_t keyhold_tick(uint8_t pressed_mask, int usb_ready, uint32_t now)
{
	progress = 0;

	if (pressed_mask == 0) {
		holding = KEYHOLD_NONE;
		blocked = 0;
		return KEYHOLD_NONE;
	}

	if (blocked)
		return KEYHOLD_NONE;

	/*
	 * Pads went down with no USB, or USB went away under a hold: nothing
	 * from this press may count, even if USB turns up before release.
	 */
	if (!usb_ready) {
		holding = KEYHOLD_NONE;
		blocked = 1;
		return KEYHOLD_NONE;
	}

	keyhold_event_t w = want(pressed_mask);

	/* SW3 alone may be the first half of a rekey hold: wait and see. */
	if (pressed_mask == SW3_MASK && holding == KEYHOLD_NONE)
		return KEYHOLD_NONE;

	if (w == KEYHOLD_NONE) {
		/* Some other combination: not ours, and not to be resumed. */
		holding = KEYHOLD_NONE;
		blocked = 1;
		return KEYHOLD_NONE;
	}

	/* A new hold, or SW3 joining an export hold: (re)start the clock. */
	if (w != holding) {
		if (holding == KEYHOLD_REKEY) {
			/* Letting SW3 go out of a rekey hold is not an export. */
			holding = KEYHOLD_NONE;
			blocked = 1;
			return KEYHOLD_NONE;
		}
		holding = w;
		t_down  = now;
	}

	uint32_t arm  = w == KEYHOLD_EXPORT ? KEY_EXPORT_ARM_MS : REKEY_ARM_MS;
	uint32_t full = w == KEYHOLD_EXPORT ? KEY_EXPORT_HOLD_MS : REKEY_HOLD_MS;
	uint32_t held = now - t_down;

	if (held >= full) {
		blocked = 1;
		holding = KEYHOLD_NONE;
		return w;
	}

	if (held >= arm)
		progress = (uint8_t)(1u + ((held - arm) * 253u) / (full - arm));
	return KEYHOLD_NONE;
}

uint8_t keyhold_progress(void)
{
	return progress;
}

keyhold_event_t keyhold_arming(void)
{
	return progress ? holding : KEYHOLD_NONE;
}
