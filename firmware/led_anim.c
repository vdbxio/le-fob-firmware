#include "ch32fun.h"
#include "board.h"
#include "bthome.h"
#include "ws2812.h"
#include "led_anim.h"
#include <string.h>

/* Refresh rate. 50 Hz is smooth to the eye and leaves the bit-bang (which
 * runs with interrupts off) using well under 1% of the time budget. */
#define FRAME_MS 20

/* Blink: on time and full cycle, per repetition. */
#define BLINK_ON_MS    100
#define BLINK_CYCLE_MS 210

/* Fade: instant on, linear decay to black. */
#define FADE_MS 500

/* Pulse: triangle-wave breathing, one cycle up and down. */
#define PULSE_CYCLE_MS 700
#define PULSE_CYCLES   2

enum {
	ANIM_IDLE = 0,
	ANIM_BLINK,
	ANIM_FADE,
	ANIM_PULSE,
};

typedef struct {
	uint8_t  kind;
	uint8_t  r, g, b; /* base colour at full scale */
	uint8_t  reps;
	uint32_t t0;
} anim_t;

static anim_t   anim[WS2812_COUNT];
static uint8_t  fb[WS2812_COUNT * 3]; /* wire order: G, R, B */
static uint8_t  fb_shadow[WS2812_COUNT * 3];
static uint32_t t_last_frame;
static uint8_t  primed; /* a frame has been pushed at least once */

/* One identity colour per pad, so a glance tells you which button fired. */
static const uint8_t pad_colour[WS2812_COUNT][3] = {
	{ 0,   255, 255 }, /* SW1 cyan    */
	{ 0,   255, 0   }, /* SW2 green   */
	{ 255, 140, 0   }, /* SW3 amber   */
	{ 255, 0,   255 }, /* SW4 magenta */
};

static const uint8_t colour_white[3] = { 255, 255, 255 };
static const uint8_t colour_red[3]   = { 255, 0,   0   };

/* Scale 0..255 by a 0..255 level and then by the global brightness cap.
 * Done in one 32-bit multiply chain to avoid two rounding steps. */
static inline uint8_t scale(uint8_t v, uint8_t level)
{
	uint32_t x = (uint32_t)v * level * PAD_LED_BRIGHTNESS_PCT;

	return (uint8_t)(x / (255u * 100u));
}

static void set_anim(uint8_t i, uint8_t kind, const uint8_t rgb[3],
                     uint8_t reps, uint32_t now)
{
	anim[i].kind = kind;
	anim[i].r    = rgb[0];
	anim[i].g    = rgb[1];
	anim[i].b    = rgb[2];
	anim[i].reps = reps;
	anim[i].t0   = now;
}

void led_anim_trigger(uint8_t button, uint8_t event, uint32_t now)
{
	if (button >= WS2812_COUNT)
		return;

	switch (event) {
	case BTHOME_EVT_PRESS:
		set_anim(button, ANIM_BLINK, pad_colour[button], 1, now);
		break;
	case BTHOME_EVT_DOUBLE_PRESS:
		set_anim(button, ANIM_BLINK, pad_colour[button], 2, now);
		break;
	case BTHOME_EVT_TRIPLE_PRESS:
		set_anim(button, ANIM_BLINK, pad_colour[button], 3, now);
		break;
	case BTHOME_EVT_LONG_PRESS:
	case BTHOME_EVT_LONG_DOUBLE_PRESS:
	case BTHOME_EVT_LONG_TRIPLE_PRESS:
		/* White fade: unmistakably not one of the tap patterns. */
		set_anim(button, ANIM_FADE, colour_white, 1, now);
		break;
	case BTHOME_EVT_HOLD_PRESS:
		set_anim(button, ANIM_PULSE, colour_red, PULSE_CYCLES, now);
		break;
	default:
		break;
	}
}

/* Compute the current 0..255 brightness of one animation, or return 0 and
 * mark it idle once it has run its course. */
static uint8_t anim_level(anim_t *a, uint32_t now)
{
	uint32_t dt = now - a->t0;

	switch (a->kind) {
	case ANIM_BLINK: {
		uint32_t total = (uint32_t)a->reps * BLINK_CYCLE_MS;

		if (dt >= total) {
			a->kind = ANIM_IDLE;
			return 0;
		}
		return (dt % BLINK_CYCLE_MS) < BLINK_ON_MS ? 255 : 0;
	}

	case ANIM_FADE:
		if (dt >= FADE_MS) {
			a->kind = ANIM_IDLE;
			return 0;
		}
		return (uint8_t)(255u - (dt * 255u) / FADE_MS);

	case ANIM_PULSE: {
		uint32_t total = (uint32_t)a->reps * PULSE_CYCLE_MS;

		if (dt >= total) {
			a->kind = ANIM_IDLE;
			return 0;
		}

		/* Triangle wave: up over the first half of the cycle, down over
		 * the second. Cheaper than a sine table and, at this brightness,
		 * indistinguishable. */
		uint32_t phase = dt % PULSE_CYCLE_MS;
		uint32_t half  = PULSE_CYCLE_MS / 2;

		if (phase < half)
			return (uint8_t)((phase * 255u) / half);
		return (uint8_t)(255u - ((phase - half) * 255u) / half);
	}

	default:
		return 0;
	}
}

void led_anim_tick(uint32_t now)
{
	if (primed && (uint32_t)(now - t_last_frame) < FRAME_MS)
		return;
	t_last_frame = now;

	for (int i = 0; i < WS2812_COUNT; i++) {
		uint8_t level = anim_level(&anim[i], now);

		/* WS2812 wire order is green, red, blue. */
		fb[i * 3 + 0] = scale(anim[i].g, level);
		fb[i * 3 + 1] = scale(anim[i].r, level);
		fb[i * 3 + 2] = scale(anim[i].b, level);
	}

	/* Only touch the chain when the picture actually changed. Every refresh
	 * costs ~120 us with interrupts disabled, and most ticks change nothing. */
	if (primed && memcmp(fb, fb_shadow, sizeof(fb)) == 0)
		return;

	memcpy(fb_shadow, fb, sizeof(fb));
	ws2812_show(fb, WS2812_COUNT);
	primed = 1;
}

int led_anim_busy(void)
{
	for (int i = 0; i < WS2812_COUNT; i++) {
		if (anim[i].kind != ANIM_IDLE)
			return 1;
	}
	return 0;
}

void led_anim_all_off(void)
{
	for (int i = 0; i < WS2812_COUNT; i++)
		anim[i].kind = ANIM_IDLE;

	memset(fb, 0, sizeof(fb));
	memcpy(fb_shadow, fb, sizeof(fb));
	ws2812_show(fb, WS2812_COUNT);
	primed = 1;
}

void led_anim_init(void)
{
	ws2812_init();
	led_anim_all_off();
}
