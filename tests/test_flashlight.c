/*
 * Host-side check of firmware/flashlight.c.
 *
 * The duty cap is the only current limiting the white LEDs have, so the
 * first thing asserted is that nothing can drive PWM6 above it. The rest is
 * the behaviour: momentary, latch on double tap, any press unlatches
 * without relighting, SW2/SW3 step the brightness while latched and stay
 * off the air, the latch times out, and a low battery refuses.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/flashlight.h"

unsigned char stub_pwm[12];

static int      failures;
static uint32_t now;

static void check(int cond, const char *what)
{
	printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

#define PAD  (1u << FLASHLIGHT_BUTTON)
#define DOWN (1u << FLASHLIGHT_DIM_BUTTON)
#define UP   (1u << FLASHLIGHT_BRIGHT_BUTTON)

#define DUTY(pct) ((255 * (pct)) / 100)

static int lit(void) { return (R8_PWM_OUT_EN & 0x14) == 0x14; }

/* Run the tick with `mask` held for `ms` milliseconds. */
static void hold(uint8_t mask, uint32_t ms)
{
	for (uint32_t i = 0; i < ms; i++)
		flashlight_tick(mask, 0, ++now);
}

/* A tap: down for 60 ms, up for 60 ms. */
static void tap(void) { hold(PAD, 60); hold(0, 60); }

int main(void)
{
	flashlight_init();

	printf("\n== duty cap ==\n");

	check(!lit(), "off after init");
	check(R8_PWM_CLOCK_DIV == 255 && R8_PWM_CONFIG == 0, "919 Hz, 8-bit cycle");
	hold(PAD, 10);
	check(lit(), "press: on");
	check(R8_PWM6_DATA == (255 * FLASHLIGHT_PCT) / 100,
	      "PWM6 duty is exactly FLASHLIGHT_PCT of the cycle");
	check(R8_PWM6_DATA <= 255 * 25 / 100, "and at or under a quarter");
	check((R8_PWM_POLAR & 0x04) == 0 && (R8_PWM_POLAR & 0x10),
	      "PWM6 normal polarity, PWM8 inverted");
	check(R8_PWM8_DATA == 256 - R8_PWM6_DATA,
	      "PWM8 low-time is the complement: same duty, other half of the cycle");
	check(R8_PWM6_DATA + (256 - R8_PWM8_DATA) <= 256,
	      "the two on-times do not overlap");
	hold(0, 10);
	check((R8_PWM_OUT_EN & 0x14) == 0, "off: both channels disabled");
	hold(PAD, 10);

	printf("\n== momentary ==\n");

	hold(PAD, 500);
	check(lit() && flashlight_on(), "stays on while held");
	hold(0, 10);
	check(!lit() && !flashlight_on(), "off on release");
	check(R8_PWM6_DATA == (255 * FLASHLIGHT_PCT) / 100,
	      "the data register keeps the cap (OUT_EN gates it)");

	printf("\n== latch ==\n");

	/* Two taps; the decoder reports the double press BTN_MULTITAP_MS after
	 * the second release, with the pad up and the light already off. */
	tap(); tap();
	check(!lit(), "dark between the second release and the decode");
	hold(0, BTN_MULTITAP_MS);
	flashlight_latch(now);
	hold(0, 10);
	check(lit() && flashlight_on(), "double tap: latched on");
	hold(0, 60000);
	check(lit(), "still on a minute later");

	printf("\n== unlatch ==\n");

	hold(PAD, 300);
	check(!lit(), "a press while latched turns it off, and does not relight");
	hold(0, 10);
	check(!lit(), "and it stays off after release");
	hold(PAD, 10);
	check(lit(), "the next press is momentary again");
	hold(0, 10);

	/* Turning it off with a double tap: the first press unlatches, the
	 * second is momentary, then the decoder says "double press". That
	 * must not switch it straight back on. */
	hold(0, FLASHLIGHT_RELATCH_GUARD_MS); flashlight_latch(now); hold(0, 10);
	check(lit(), "(latched again for the next check)");
	tap(); tap();
	hold(0, BTN_MULTITAP_MS);
	flashlight_latch(now);
	hold(0, 10);
	check(!lit(), "a double tap that started by unlatching stays off");
	hold(0, FLASHLIGHT_RELATCH_GUARD_MS);
	tap(); tap(); hold(0, BTN_MULTITAP_MS); flashlight_latch(now); hold(0, 10);
	check(lit(), "a double tap after the guard window latches again");
	hold(PAD, 20); hold(0, 20);
	check(!lit(), "(press to clear it)");

	printf("\n== brightness ==\n");

	hold(0, FLASHLIGHT_RELATCH_GUARD_MS);
	flashlight_latch(now); hold(0, 10);
	check(lit() && R8_PWM6_DATA == DUTY(FLASHLIGHT_PCT),
	      "a latch starts at FLASHLIGHT_PCT");
	hold(UP, 1);
	check(R8_PWM6_DATA == DUTY(FLASHLIGHT_PCT + FLASHLIGHT_STEP_PCT),
	      "SW3: one step up, on the press edge");
	check(R8_PWM8_DATA == 256 - R8_PWM6_DATA, "PWM8 keeps the complement");
	hold(UP, 500); hold(0, 60);
	check(R8_PWM6_DATA == DUTY(FLASHLIGHT_PCT + FLASHLIGHT_STEP_PCT),
	      "holding it is still one step");
	check(flashlight_took_press(FLASHLIGHT_BRIGHT_BUTTON),
	      "the press is taken from the radio");
	check(!flashlight_took_press(FLASHLIGHT_BRIGHT_BUTTON), "(once)");
	for (int i = 0; i < 12; i++) { hold(UP, 60); hold(0, 60); }
	check(R8_PWM6_DATA == DUTY(FLASHLIGHT_MAX_PCT), "clamps at FLASHLIGHT_MAX_PCT");
	check(R8_PWM6_DATA <= DUTY(90), "which is at or under 90%");
	flashlight_took_press(FLASHLIGHT_BRIGHT_BUTTON);
	hold(DOWN, 60); hold(0, 60);
	check(R8_PWM6_DATA == DUTY(FLASHLIGHT_MAX_PCT - FLASHLIGHT_STEP_PCT),
	      "SW2: one step down");
	check(flashlight_took_press(FLASHLIGHT_DIM_BUTTON), "also taken from the radio");
	for (int i = 0; i < 12; i++) { hold(DOWN, 60); hold(0, 60); }
	check(R8_PWM6_DATA == DUTY(FLASHLIGHT_STEP_PCT), "clamps at one step, never dark");
	check(lit(), "still latched on through all of that");
	flashlight_took_press(FLASHLIGHT_DIM_BUTTON);
	hold(UP, 60); hold(0, 60); hold(UP, 60); hold(0, 60);
	flashlight_took_press(FLASHLIGHT_BRIGHT_BUTTON);
	hold(PAD, 50); hold(0, 60);
	check(!lit(), "SW4 still switches it off");
	hold(DOWN, 60); hold(0, 60);
	check(!flashlight_took_press(FLASHLIGHT_DIM_BUTTON),
	      "unlatched, SW2 is a remote button again");
	hold(PAD, 10);
	check(lit() && R8_PWM6_DATA == DUTY(FLASHLIGHT_PCT),
	      "momentary is back at FLASHLIGHT_PCT");
	hold(0, 10);
	hold(0, FLASHLIGHT_RELATCH_GUARD_MS);
	flashlight_latch(now); hold(0, 10);
	check(R8_PWM6_DATA == DUTY(FLASHLIGHT_PCT), "the next latch starts at FLASHLIGHT_PCT again");
	hold(PAD, 20); hold(0, 20);
	check(!lit(), "(press to clear it)");

	printf("\n== timeout ==\n");

	hold(0, FLASHLIGHT_RELATCH_GUARD_MS);
	flashlight_latch(now);
	hold(0, FLASHLIGHT_LATCH_MAX_MS - 1000);
	check(lit(), "latched: on just before the limit");
	hold(0, 2000);
	check(!lit(), "off by itself at FLASHLIGHT_LATCH_MAX_MS");

	printf("\n== battery low ==\n");

	for (int i = 0; i < 20; i++)
		flashlight_tick(PAD, 1, ++now);
	check(!lit(), "refused while the battery is low");
	hold(0, FLASHLIGHT_RELATCH_GUARD_MS);
	flashlight_latch(now);
	for (int i = 0; i < 20; i++)
		flashlight_tick(0, 1, ++now);
	check(!lit(), "and cannot be latched on either");
	for (int i = 0; i < 20; i++)
		flashlight_tick(0, 0, ++now);
	check(!lit(), "a latch refused for battery does not fire later");
	for (int i = 0; i < 20; i++)
		flashlight_tick(PAD, 0, ++now);
	check(lit(), "lights again once the battery recovers");
	hold(0, 10);

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
