/*
 * Host-side check of the pad LED layering in firmware/led_anim.c.
 *
 * ws2812_init/ws2812_show are stubbed here to capture the framebuffer
 * instead of driving hardware, so the real animation engine can be driven
 * through its states and the resulting pixels asserted on. Nothing about the
 * layering is reimplemented.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/bthome.h"
#include "../firmware/led_anim.h"

uint8_t g_fake_rom[6];

/* --- captured frame ------------------------------------------------- */
static uint8_t frame[WS2812_COUNT * 3]; /* wire order: G, R, B */
static int     frames;

void ws2812_init(void) {}
void ws2812_show(const uint8_t *grb, int count)
{
	memcpy(frame, grb, (size_t)count * 3);
	frames++;
}

/* Pixel accessors, translating out of the GRB wire order. */
static uint8_t R(int i) { return frame[i * 3 + 1]; }
static uint8_t G(int i) { return frame[i * 3 + 0]; }
static uint8_t B(int i) { return frame[i * 3 + 2]; }
static int     lit(int i) { return R(i) || G(i) || B(i); }

static int      failures;
static uint32_t now;

static void check(int cond, const char *what)
{
	printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

/* Advance past the animation engine's frame rate limiter. */
static void render(uint32_t ms)
{
	now += ms;
	led_anim_tick(now);
}

static int all_dark(void)
{
	for (int i = 0; i < WS2812_COUNT; i++)
		if (lit(i))
			return 0;
	return 1;
}

/* Brightest channel anywhere in the frame. */
static uint8_t peak(void)
{
	uint8_t m = 0;

	for (unsigned i = 0; i < sizeof(frame); i++)
		if (frame[i] > m)
			m = frame[i];
	return m;
}

int main(void)
{
	led_anim_init();
	render(50);

	printf("\n== at rest ==\n");
	check(all_dark(), "all pads dark with nothing happening");

	printf("\n== press layer ==\n");

	led_anim_set_pressed(1u << 2); /* SW3 held */
	render(50);
	check(lit(2), "held pad lights immediately");
	check(!lit(0) && !lit(1) && !lit(3), "other pads stay dark");

	uint8_t press_peak = peak();

	/* SW3's identity colour is amber (255,140,0): red and green, no blue. */
	check(R(2) && G(2) && !B(2), "held pad shows its identity colour");

	led_anim_set_pressed(0);
	render(50);
	check(all_dark(), "release returns the pad to dark");

	printf("\n== gesture layer ==\n");

	led_anim_trigger(0, BTHOME_EVT_PRESS, now);
	render(30);
	check(lit(0), "gesture lights its pad");
	check(peak() > press_peak,
	      "gesture is brighter than the press glow");

	/* SW1 is cyan (0,255,255): green and blue, no red. */
	check(!R(0) && G(0) && B(0), "gesture uses the pad identity colour");

	/* One blink is BLINK_CYCLE_MS; well past it the pad is dark again. */
	render(400);
	check(all_dark(), "gesture animation ends dark");

	printf("\n== gesture outranks press ==\n");

	led_anim_set_pressed(1u << 3);
	led_anim_trigger(3, BTHOME_EVT_LONG_PRESS, now);
	render(30);
	/* Long press is a white fade; the press glow would be magenta. */
	check(R(3) && G(3) && B(3),
	      "held pad running a gesture shows the gesture, not the glow");

	led_anim_set_pressed(0);
	render(600);
	check(all_dark(), "and settles dark once the fade completes");

	printf("\n== tx layer ==\n");

	/* A burst with no gesture behind it: the white on-air glow. */
	led_anim_set_tx(1);
	render(50);
	check(lit(1), "burst with no gesture lights its pad");
	check(R(1) && G(1) && B(1), "on-air glow is white");

	led_anim_set_tx(-1);
	render(50);
	check(all_dark(), "pad goes dark when the burst ends");

	/* The normal case: gesture and burst together. The gesture pattern is
	 * the on-air indication, so the white glow must not also appear --
	 * including in the window where the burst outlives the pattern. */
	led_anim_trigger(2, BTHOME_EVT_PRESS, now);
	led_anim_set_tx(2);
	render(30);
	check(R(2) && G(2) && !B(2), "gesture colour, not white, while on air");

	render(400); /* past the 210 ms blink, burst still notionally running */
	check(!lit(2), "no white artifact after the pattern ends");

	led_anim_set_tx(-1);
	render(50);
	check(all_dark(), "dark once everything finishes");

	/* Suppression must not leak into the next burst. */
	led_anim_set_tx(2);
	render(50);
	check(lit(2), "a later gesture-less burst is not suppressed");
	led_anim_set_tx(-1);
	render(50);

	printf("\n== brightness ceiling ==\n");

	/*
	 * A blink sits at full level for its whole on-phase, so it samples the
	 * ceiling exactly. A fade would already have decayed a few percent by
	 * the time the first frame renders.
	 */
	led_anim_trigger(0, BTHOME_EVT_PRESS, now);
	render(30);

	int ceiling = (255 * PAD_LED_BRIGHTNESS_PCT) / 100;

	printf("  brightest channel %d, ceiling %d (%d%%)\n",
	       peak(), ceiling, PAD_LED_BRIGHTNESS_PCT);
	check(peak() == ceiling, "a full-level gesture hits the ceiling exactly");

	/* Let the blink finish before measuring idle behaviour. */
	render(500);
	check(all_dark(), "settled dark before the idle measurement");

	printf("\n== refresh economy ==\n");

	int before = frames;

	for (int i = 0; i < 20; i++)
		render(50); /* nothing is changing; the chain should be left alone */
	printf("  %d refreshes over 20 idle frames\n", frames - before);
	check(frames - before == 0, "idle frames do not touch the chain");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
