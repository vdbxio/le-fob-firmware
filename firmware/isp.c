/*
 * Bootloader-entry hold tracker. See isp.h for why this exists.
 */

#include "isp.h"
#include "board.h"

#define SW1_MASK 0x01u

static uint32_t t_down;   /* when SW1-alone started */
static uint8_t  holding;  /* SW1-alone is currently down */
static uint8_t  fired;    /* jumped (or would have); wait for release */
static uint8_t  progress;

int isp_hold_tick(uint8_t pressed_mask, uint32_t now)
{
	/*
	 * Anything other than exactly SW1 cancels. A second pad, or nothing
	 * at all, both mean this is not the gesture, and either one also
	 * clears the fired latch so the next attempt is allowed.
	 */
	if (pressed_mask != SW1_MASK) {
		holding  = 0;
		progress = 0;
		if (pressed_mask == 0)
			fired = 0;
		return 0;
	}

	if (!holding) {
		holding = 1;
		t_down  = now;
	}

	if (fired)
		return 0;

	uint32_t held = now - t_down;

	if (held < ISP_ARM_MS) {
		progress = 0;
		return 0;
	}

	if (held >= ISP_HOLD_MS) {
		/* The fuse has done its job; whatever happens next (the jump,
		 * or a refusal) draws its own picture. */
		progress = 0;
		fired    = 1;
		return 1;
	}

	/* 1..254 across the arming window; 0 is reserved for "not arming". */
	uint32_t span = ISP_HOLD_MS - ISP_ARM_MS;
	uint32_t p    = 1u + ((held - ISP_ARM_MS) * 253u) / span;
	progress      = (uint8_t)p;
	return 0;
}

uint8_t isp_hold_progress(void)
{
	return progress;
}
