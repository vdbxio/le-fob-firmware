/*
 * Host-side check of the bootloader-entry hold in firmware/isp.c.
 *
 * The thing that must never happen is a jump into the bootloader that the
 * user did not ask for: from a normal hold gesture, from a two-pad press,
 * or twice from one hold. The thing that must always happen is a jump from
 * a deliberate SW1-alone hold, every time, including right after a refused
 * one. Both are cheap to prove by feeding the tracker a pad mask a
 * millisecond at a time.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/isp.h"

static int      failures;
static uint32_t now;

static void check(int cond, const char *what)
{
	printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

/*
 * Hold `mask` for `ms` milliseconds; return how many ticks reported "fire".
 *
 * A change of mask costs one extra tick first -- the one on which the
 * tracker notices the new state and starts its clock -- so that
 * hold(SW1, N) means "SW1 has been down for N ms" by the time it returns.
 */
static uint8_t last_mask;

static int hold(uint8_t mask, uint32_t ms)
{
	int fires = 0;

	if (mask != last_mask) {
		fires += isp_hold_tick(mask, ++now);
		last_mask = mask;
	}
	for (uint32_t i = 0; i < ms; i++)
		fires += isp_hold_tick(mask, ++now);
	return fires;
}

int main(void)
{
	printf("test_isp\n");

	/* A normal hold_press gesture is nowhere near the fuse. */
	hold(0, 10);
	check(hold(0x01, BTN_HOLD_MS + 100) == 0 && isp_hold_progress() == 0,
	      "hold_press-length SW1 hold: no arming, no fire");
	hold(0, 10);

	/* The fuse lights at ISP_ARM_MS and grows monotonically. */
	hold(0x01, ISP_ARM_MS - 1);
	check(isp_hold_progress() == 0, "progress 0 just before ISP_ARM_MS");
	hold(0x01, 1);
	check(isp_hold_progress() >= 1, "progress nonzero at ISP_ARM_MS");
	{
		uint8_t last = isp_hold_progress();
		int     mono = 1;
		for (uint32_t t = ISP_ARM_MS; t < ISP_HOLD_MS - 1; t++) {
			int f = isp_hold_tick(0x01, ++now);
			if (f || isp_hold_progress() < last)
				mono = 0;
			last = isp_hold_progress();
		}
		check(mono && last > 200, "progress climbs without firing");
	}

	/* Fires exactly once, on the tick the hold length is reached... */
	check(hold(0x01, 1) == 1, "fires at ISP_HOLD_MS");
	check(isp_hold_progress() == 0, "fuse cleared on fire");
	/* ...and never again while still held. */
	check(hold(0x01, 10000) == 0 && isp_hold_progress() == 0,
	      "no re-fire while SW1 stays down");

	/* Release, then a fresh hold fires again from scratch. */
	hold(0, 5);
	check(hold(0x01, ISP_HOLD_MS - 1) == 0, "fresh hold does not fire early");
	check(hold(0x01, 1) == 1, "fresh hold fires at ISP_HOLD_MS");
	hold(0, 5);

	/* A second pad cancels, and the clock restarts on its release. */
	hold(0x01, ISP_ARM_MS + 500);
	check(isp_hold_progress() > 0, "arming before second pad");
	hold(0x03, 100);
	check(isp_hold_progress() == 0, "second pad clears the fuse");
	/* Without a restart this would already be well past ISP_ARM_MS. */
	hold(0x01, ISP_ARM_MS - 1);
	check(isp_hold_progress() == 0, "SW1 clock restarted after the combo");
	check(hold(0x01, ISP_HOLD_MS - ISP_ARM_MS) == 0,
	      "restarted hold does not fire early");
	check(hold(0x01, 1) == 1, "restarted hold fires at ISP_HOLD_MS");
	hold(0, 5);

	/* Any other pad alone never gets anywhere. */
	check(hold(0x02, ISP_HOLD_MS * 2) == 0 && isp_hold_progress() == 0,
	      "SW2 alone never arms");
	check(hold(0x0E, ISP_HOLD_MS * 2) == 0 && isp_hold_progress() == 0,
	      "SW2+SW3+SW4 never arm");
	hold(0, 5);

	/* Two pads from the start, including SW1, never arm either. */
	check(hold(0x09, ISP_HOLD_MS * 2) == 0 && isp_hold_progress() == 0,
	      "SW1+SW4 never arm");
	hold(0, 5);

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
