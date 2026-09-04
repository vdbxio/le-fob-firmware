/*
 * Host-side check of the gesture state machine in firmware/buttons.c.
 *
 * The real buttons.c is compiled natively; the GPIO registers it reads are
 * plain variables here (tests/stub/ch32fun.h), so a test can drive the pads
 * millisecond by millisecond and assert on what comes out of the event
 * queue. Nothing about the decoder is reimplemented.
 *
 * Expected strings use buttons_event_name()'s short log names ("double",
 * "long_double"), not the longer BTHome spec names for the same codes.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/bthome.h"
#include "../firmware/buttons.h"

/* Storage backing the stubbed registers. */
uint8_t  g_fake_rom[6];
uint32_t R32_PA_PIN = 0xFFFFFFFF, R32_PB_PIN = 0xFFFFFFFF;
uint32_t R32_PA_CLR, R32_PB_CLR;
uint16_t R16_PA_INT_EN, R16_PB_INT_EN;
uint16_t R16_PA_INT_MODE, R16_PB_INT_MODE;
uint16_t R16_PA_INT_IF, R16_PB_INT_IF;
uint16_t R16_PIN_ALTERNATE;

static int      failures;
static uint32_t now;

/* Which port/bit each button lives on, mirroring board.h. */
static void press(int b, int down)
{
	static const uint32_t pb_bit[4] = { 1u << 22, 1u << 4, 1u << 23, 0 };

	if (b == 3) { /* SW4 is PA9 */
		if (down) R32_PA_PIN &= ~(1u << 9);
		else      R32_PA_PIN |= (1u << 9);
	} else {
		if (down) R32_PB_PIN &= ~pb_bit[b];
		else      R32_PB_PIN |= pb_bit[b];
	}
}

/* Run the decoder forward n milliseconds. */
static void advance(uint32_t ms)
{
	for (uint32_t i = 0; i < ms; i++)
		buttons_tick(now++);
}

static void hold(int b, uint32_t ms)  { press(b, 1); advance(ms); }
static void release(int b, uint32_t ms) { press(b, 0); advance(ms); }

/* Drain the queue into a printable summary like "btn1:press". */
static void drain(char *out, size_t cap)
{
	uint8_t b, e;

	out[0] = 0;
	while (buttons_pop_event(&b, &e)) {
		char one[48];

		snprintf(one, sizeof(one), "%sbtn%d:%s",
		         out[0] ? " " : "", b + 1, buttons_event_name(e));
		strncat(out, one, cap - strlen(out) - 1);
	}
}

static void expect(const char *what, const char *want)
{
	char got[256];

	drain(got, sizeof(got));

	int ok = strcmp(got, want) == 0;

	printf("  %-46s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok) {
		printf("      expected: \"%s\"\n      got:      \"%s\"\n", want, got);
		failures++;
	}
}

/* Settle: long enough for any pending multi-tap window to resolve. */
static void settle(void) { advance(BTN_MULTITAP_MS + 60); }

int main(void)
{
	buttons_init();
	advance(20);
	drain((char[256]){0}, 256); /* discard anything from init */

	printf("\n== taps ==\n");

	hold(0, 40); release(0, 0); settle();
	expect("single tap -> press", "btn1:press");

	hold(1, 40); release(1, 60); hold(1, 40); release(1, 0); settle();
	expect("two taps -> double_press", "btn2:double");

	hold(2, 40); release(2, 60); hold(2, 40); release(2, 60);
	hold(2, 40); release(2, 0); settle();
	expect("three taps -> triple_press", "btn3:triple");

	printf("\n== holds ==\n");

	/* Held past BTN_LONG_MS: the event must fire while still held, not on
	 * release, so add no extra time before checking. */
	hold(3, BTN_LONG_MS + 30);
	expect("long hold fires before release", "btn4:long");
	release(3, 0); settle();
	expect("release after long adds nothing", "");

	hold(0, BTN_HOLD_MS + 30);
	expect("longer hold -> long_press then hold_press",
	       "btn1:long btn1:hold");
	release(0, 0); settle();
	expect("release after hold adds nothing", "");

	printf("\n== taps banked into a hold ==\n");

	hold(1, 40); release(1, 60); hold(1, BTN_LONG_MS + 30);
	expect("tap then hold -> long_double_press", "btn2:long_double");
	release(1, 0); settle();
	expect("nothing trails it", "");

	hold(2, 40); release(2, 60); hold(2, 40); release(2, 60);
	hold(2, BTN_LONG_MS + 30);
	expect("two taps then hold -> long_triple_press",
	       "btn3:long_triple");
	release(2, 0); settle();
	expect("nothing trails it", "");

	printf("\n== debounce ==\n");

	/* A contact bounce shorter than BTN_DEBOUNCE_MS must vanish. */
	hold(0, 3); release(0, 0); settle();
	expect("3 ms glitch is rejected", "");

	/* A press that bounces on the way down is still one press. */
	press(0, 1); advance(2);
	press(0, 0); advance(2);
	press(0, 1); advance(3);
	press(0, 0); advance(2);
	press(0, 1); advance(40);
	release(0, 0); settle();
	expect("bouncy press still reads as one tap", "btn1:press");

	printf("\n== independence ==\n");

	hold(0, 40); release(0, 0);
	hold(3, 40); release(3, 0);
	settle();
	expect("two pads report separately", "btn1:press btn4:press");

	printf("\n== busy / idle ==\n");

	press(0, 1);
	advance(20);
	int busy_down = buttons_busy();

	release(0, 0);
	settle();
	drain((char[256]){0}, 256);
	int idle_after = buttons_busy();

	printf("  %-46s %s\n", "busy while a pad is down",
	       busy_down ? "ok" : "FAIL");
	failures += !busy_down;
	printf("  %-46s %s\n", "idle once everything has resolved",
	       !idle_after ? "ok" : "FAIL");
	failures += !!idle_after;

	printf("\n== interrupt setup ==\n");

	int intx = (R16_PIN_ALTERNATE & RB_PIN_INTX) != 0;
	/* PB22 -> int bit 8, PB23 -> int bit 9, PB4 -> bit 4. */
	int pb_en = R16_PB_INT_EN == ((1u << 8) | (1u << 9) | (1u << 4));
	int pa_en = R16_PA_INT_EN == (1u << 9);

	printf("  %-46s %s\n", "RB_PIN_INTX set for PB22/PB23", intx ? "ok" : "FAIL");
	printf("  %-46s %s\n", "PB interrupts armed on bits 4, 8, 9", pb_en ? "ok" : "FAIL");
	printf("  %-46s %s\n", "PA interrupt armed on bit 9", pa_en ? "ok" : "FAIL");
	/* Arming GPIO as a wake source is power.c's job and is asserted in
	 * test_power.c -- it needs more than one bit, and splitting it across
	 * two files is what let an incomplete version ship. */
	failures += !intx + !pb_en + !pa_en;

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
