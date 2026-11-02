#include "ch32fun.h"
#include "board.h"
#include "bthome.h"
#include "ws2812.h"
#include "led_anim.h"
#include <string.h>

/* Refresh rate. 50 Hz is smooth to the eye and leaves the bit-bang (which
 * runs with interrupts off) using well under 1% of the time budget. */
#define FRAME_MS 20

/*
 * Push the frame again this often even when nothing has changed.
 *
 * Skipping unchanged frames is a real saving, but on its own it means a
 * single lost frame is permanent: if the chain misses the all-dark frame,
 * the framebuffer still matches the shadow, so nothing is ever re-sent and
 * the pads stay lit at whatever they were showing. A keepalive bounds that
 * to one interval, at a cost of ~120 us every quarter second.
 */
#define KEEPALIVE_MS 250

/*
 * Note on what is deliberately NOT here.
 *
 * An earlier version sent the frame twice whenever more than 100 ms had
 * passed since the last write, on the theory that a V5/V6 part may drop
 * into its low-power state and swallow the frame that wakes it. That was
 * speculation, and it was actively wrong: the blink phases are 100 ms on
 * and 110 ms off, so *every* transition during an animation cleared that
 * threshold and got sent twice -- visible as a stray extra flash partway
 * through a multi-blink gesture.
 *
 * It was also redundant. The keepalive above means the chain is written at
 * least every 250 ms and so never sits idle long enough to sleep, and it
 * already bounds the damage from any single dropped frame. If a sleep-wake
 * problem ever does show up, the fix belongs at boot (see led_anim_init)
 * or in the keepalive interval, not in a per-write heuristic that cannot
 * tell an idle chain from an animating one.
 */

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

/* Levels for the two non-gesture layers, as a 0..255 multiplier that then
 * goes through the global PAD_LED_BRIGHTNESS_PCT ceiling in scale(). */
#define PRESS_LEVEL  ((255 * PAD_LED_PRESS_PCT) / 100)
#define TX_LEVEL     ((255 * PAD_LED_TX_PCT) / 100)
#define CHARGE_LEVEL ((255 * PAD_LED_CHARGE_PCT) / 100)

/* Charge indicator timing: one breath while charging, one blink on fault. */
#define CHARGE_BREATHE_MS 3000
#define CHARGE_BLINK_MS   2000
#define CHARGE_PIXEL      0      /* pad 1, nearest the USB port */

static anim_t   anim[WS2812_COUNT];
static uint8_t  pressed_mask;
static int8_t   tx_button = -1;

/*
 * Pads whose gesture animation already stands for the burst now on air.
 *
 * Every advertisement today is triggered by a gesture, and the gesture
 * pattern runs concurrently with it -- so that pattern *is* the "on air"
 * indication, and painting the white TX glow on top would only show up in
 * the gap where the burst outlives the pattern. A single tap is exactly that
 * case: one blink is 210 ms against a 240 ms burst, so without this the
 * cyan blink would be chased by a stray white tick.
 *
 * Set when a gesture animation starts, cleared when the burst ends. What is
 * left is the case this layer is actually for: a chirp with no gesture
 * behind it. There are none yet -- battery and status chirps would be the
 * first -- so on current firmware the white glow stays unused by design.
 */
static uint8_t  tx_suppress;
static uint8_t  charge_mode;
static uint8_t  fb[WS2812_COUNT * 3]; /* wire order: G, R, B */
static uint8_t  fb_shadow[WS2812_COUNT * 3];
static uint32_t t_last_frame;
static uint32_t t_last_push; /* last time the chain was actually written */
static uint8_t  primed;      /* a frame has been pushed at least once */

static void push(uint32_t now)
{
	ws2812_show(fb, WS2812_COUNT);
	t_last_push = now;
	primed      = 1;
}

/* One identity colour per pad, so a glance tells you which button fired. */
static const uint8_t pad_colour[WS2812_COUNT][3] = {
	{ 0,   255, 255 }, /* SW1 cyan    */
	{ 0,   255, 0   }, /* SW2 green   */
	{ 255, 140, 0   }, /* SW3 amber   */
	{ 255, 0,   255 }, /* SW4 magenta */
};

static const uint8_t colour_white[3] = { 255, 255, 255 };
static const uint8_t colour_red[3]   = { 255, 0,   0   };
static const uint8_t colour_magenta[3] = { 255, 0, 255 };
static const uint8_t colour_green[3] = { 0,   255, 0   };

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

void led_anim_set_pressed(uint8_t mask)
{
	pressed_mask = mask;
}

void led_anim_set_charge(led_charge_t mode)
{
	charge_mode = (uint8_t)mode;
}

/*
 * Level of the charge indicator at `now`, 0..CHARGE_LEVEL. Breathing is a
 * triangle wave that never quite reaches dark, so the pad reads as "on and
 * doing something" rather than flashing.
 */
static uint8_t charge_level(uint32_t now)
{
	switch (charge_mode) {
	case LED_CHARGE_CHARGING: {
		uint32_t t    = now % CHARGE_BREATHE_MS;
		uint32_t half = CHARGE_BREATHE_MS / 2;
		uint32_t up   = t < half ? t : CHARGE_BREATHE_MS - t; /* 0..half */
		uint32_t lvl  = (CHARGE_LEVEL / 5) + (up * (CHARGE_LEVEL * 4 / 5)) / half;

		return (uint8_t)lvl;
	}
	case LED_CHARGE_DONE:
		return CHARGE_LEVEL;
	case LED_CHARGE_FAULT:
		return (now % CHARGE_BLINK_MS) < 200 ? CHARGE_LEVEL : 0;
	default:
		return 0;
	}
}

static const uint8_t *charge_colour(void)
{
	switch (charge_mode) {
	case LED_CHARGE_CHARGING: return colour_magenta;
	case LED_CHARGE_DONE:     return colour_green;
	case LED_CHARGE_FAULT:    return colour_red;
	default:                  return colour_white;
	}
}

void led_anim_set_tx(int button)
{
	if (button < 0)
		tx_suppress = 0; /* burst over; next one starts unsuppressed */

	tx_button = (int8_t)button;
}

static uint8_t arming;

void led_anim_set_arming(uint8_t progress)
{
	arming = progress;
}

void led_anim_trigger(uint8_t button, uint8_t event, uint32_t now)
{
	if (button >= WS2812_COUNT)
		return;

	tx_suppress |= (uint8_t)(1u << button);

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
		const uint8_t *rgb;
		uint8_t        level;

		/*
		 * anim_level() clears kind once an animation has run its course,
		 * so testing kind *after* calling it is what decides whether this
		 * pixel still belongs to the gesture layer.
		 */
		uint8_t anim_lvl = anim_level(&anim[i], now);

		if (arming) {
			/*
			 * Bootloader fuse: pad i lights once progress has crossed
			 * its share of the window, so the four fill in SW1..SW4.
			 * Everything else is hidden while this is up.
			 */
			int lit = (arming * WS2812_COUNT) > (i * 255);

			fb[i * 3 + 0] = lit ? scale(colour_white[1], TX_LEVEL) : 0;
			fb[i * 3 + 1] = lit ? scale(colour_white[0], TX_LEVEL) : 0;
			fb[i * 3 + 2] = lit ? scale(colour_white[2], TX_LEVEL) : 0;
			continue;
		}

		if (anim[i].kind != ANIM_IDLE) {
			/* Gesture owns the pixel, including its dark phases. */
			uint8_t c[3] = { anim[i].r, anim[i].g, anim[i].b };

			fb[i * 3 + 0] = scale(c[1], anim_lvl);
			fb[i * 3 + 1] = scale(c[0], anim_lvl);
			fb[i * 3 + 2] = scale(c[2], anim_lvl);
			continue;
		}

		if (PAD_LED_SHOW_TX && tx_button == i &&
		    !(tx_suppress & (1u << i))) {
			/* On air. White, to read as status rather than as one of
			 * the per-pad identity colours. */
			rgb   = colour_white;
			level = TX_LEVEL;
		} else if (PAD_LED_SHOW_PRESS && (pressed_mask & (1u << i))) {
			rgb   = pad_colour[i];
			level = PRESS_LEVEL;
		} else if (PAD_LED_SHOW_CHARGE && i == CHARGE_PIXEL && charge_mode) {
			/* Charging state, under everything else on pad 1. */
			rgb   = charge_colour();
			level = charge_level(now);
		} else {
			/* Dark at rest. */
			fb[i * 3 + 0] = 0;
			fb[i * 3 + 1] = 0;
			fb[i * 3 + 2] = 0;
			continue;
		}

		/* WS2812 wire order is green, red, blue. */
		fb[i * 3 + 0] = scale(rgb[1], level);
		fb[i * 3 + 1] = scale(rgb[0], level);
		fb[i * 3 + 2] = scale(rgb[2], level);
	}

	/*
	 * Skip the write when the picture has not changed -- most ticks change
	 * nothing and every refresh costs ~120 us with interrupts disabled --
	 * but not indefinitely, so a dropped frame cannot leave the pads stuck.
	 */
	int changed = memcmp(fb, fb_shadow, sizeof(fb)) != 0;

	if (primed && !changed &&
	    (uint32_t)(now - t_last_push) < KEEPALIVE_MS)
		return;

	memcpy(fb_shadow, fb, sizeof(fb));
	push(now);
}

int led_anim_busy(void)
{
	for (int i = 0; i < WS2812_COUNT; i++) {
		if (anim[i].kind != ANIM_IDLE)
			return 1;
	}
	return 0;
}

void led_anim_invalidate(void)
{
	primed = 0;
}

void led_anim_all_off(void)
{
	for (int i = 0; i < WS2812_COUNT; i++)
		anim[i].kind = ANIM_IDLE;

	pressed_mask = 0;
	tx_button    = -1;
	tx_suppress  = 0;
	arming       = 0;

	memset(fb, 0, sizeof(fb));
	memcpy(fb_shadow, fb, sizeof(fb));

	ws2812_show(fb, WS2812_COUNT);
	primed      = 1;
	t_last_push = 0;
}

void led_anim_init(void)
{
	ws2812_init();

	/*
	 * Blank the chain several times before trusting it.
	 *
	 * At this point the LEDs have only just come up with the rest of the
	 * board, and a V5/V6 part may still be settling or asleep. A single
	 * blanking frame that lands too early is simply lost, and because
	 * led_anim_tick() then sees an unchanged framebuffer it would never
	 * resend one -- leaving whatever the pixels powered up showing, lit,
	 * forever. Each call ends with the >280 us reset, so these latch
	 * separately.
	 */
	for (int i = 0; i < 3; i++)
		led_anim_all_off();
}
