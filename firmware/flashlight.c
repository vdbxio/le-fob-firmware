#include "ch32fun.h"
#include "board.h"
#include "flashlight.h"

/*
 * CH592 PWM4..PWM11 block. ch32fun's ch5xxhw.h carries the CH57x layout
 * for this peripheral (PWM1..5, a DMA register at +3), so the registers
 * that differ are named here from WCH's CH592SFR.h:
 *
 *   0x40005000  R8_PWM_OUT_EN     bit n enables PWM(4+n)
 *   0x40005001  R8_PWM_POLAR      0 = low at rest, high for `data` clocks
 *                                 1 = high at rest, low for `data` clocks
 *   0x40005002  R8_PWM_CONFIG     low nibble 0 = 256-clock cycle, 8-bit
 *   0x40005003  R8_PWM_CLOCK_DIV  Fsys / this
 *   0x40005006  R8_PWM6_DATA
 *   0x40005008  R8_PWM8_DATA
 */
#ifndef R8_PWM_CLOCK_DIV
#define R8_PWM_CLOCK_DIV (*((vu8 *)0x40005003))
#endif
#ifndef R8_PWM6_DATA
#define R8_PWM6_DATA     (*((vu8 *)0x40005006))
#endif
#ifndef R8_PWM8_DATA
#define R8_PWM8_DATA     (*((vu8 *)0x40005008))
#endif
#define PWM6_BIT         0x04
#define PWM8_BIT         0x10

#define CH_A ((FLASHLIGHT_CHANNELS & 1) ? PWM6_BIT : 0)
#define CH_B ((FLASHLIGHT_CHANNELS & 2) ? PWM8_BIT : 0)
#define CH_MASK (CH_A | CH_B)
_Static_assert(CH_MASK != 0, "FLASHLIGHT_CHANNELS selects no channel");

/*
 * 60 MHz / 255 / 256 = 919 Hz. Above flicker, and slow enough that the
 * gates driven through R7/R9 spend almost none of each cycle switching.
 */
#define PWM_DIV 255

_Static_assert(FLASHLIGHT_STEP_PCT >= 1 && FLASHLIGHT_STEP_PCT <= 50,
               "FLASHLIGHT_STEP_PCT is a step in percent");
_Static_assert(FLASHLIGHT_MAX_PCT >= FLASHLIGHT_STEP_PCT && FLASHLIGHT_MAX_PCT <= 100,
               "FLASHLIGHT_MAX_PCT is the duty cap in percent");
_Static_assert(FLASHLIGHT_PCT >= FLASHLIGHT_STEP_PCT && FLASHLIGHT_PCT <= FLASHLIGHT_MAX_PCT,
               "FLASHLIGHT_PCT must sit between the step and the cap");
_Static_assert(FLASHLIGHT_DIM_BUTTON != FLASHLIGHT_BUTTON &&
               FLASHLIGHT_BRIGHT_BUTTON != FLASHLIGHT_BUTTON &&
               FLASHLIGHT_DIM_BUTTON != FLASHLIGHT_BRIGHT_BUTTON,
               "the torch, dim and bright pads must be three different pads");

/* Duty count for a percentage, 0..255. 100% is 255, which the 8-bit cycle
 * renders as 255 of 256 clocks: as close to solid as the block gets. */
#define DUTY(pct) ((uint8_t)((255 * (pct)) / 100))

static uint8_t  on;
static uint8_t  latched;
static uint8_t  level_pct = FLASHLIGHT_PCT;  /* what the light is or would be driven at */
static uint8_t  was_pressed;
static uint8_t  hold_off;     /* this press turned the latch off: stay dark until release */
static uint8_t  took_mask;    /* pads whose last press was a brightness step */
static uint32_t t_latched;
static uint32_t t_unlatched;  /* when a press last turned the latch off */
static uint8_t  ever_unlatched;

/*
 * The two channels are interleaved, not in phase. All PWM4..11 channels
 * count the same 256-clock cycle, so PWM6 at normal polarity is high for
 * the first `duty` clocks, and PWM8 at inverted polarity with data
 * 256 - duty is low for the first 256 - duty clocks and high for the last
 * `duty`. Up to 50% the two on-times never overlap: same average light,
 * half the peak current off a 100 mAh cell, and half the rail sag. Above
 * 50% they overlap in the middle of the cycle for (2 x duty - 100)% of it,
 * which is when both LEDs draw at once.
 */
static void set_level(uint8_t pct)
{
	if (pct > FLASHLIGHT_MAX_PCT)
		pct = FLASHLIGHT_MAX_PCT; /* the cap, whatever asked */
	level_pct    = pct;
	R8_PWM6_DATA = DUTY(pct);
	R8_PWM8_DATA = (uint8_t)(256 - DUTY(pct));
}

static void drive(int enable)
{
	if (enable) {
		set_level(level_pct);
		R8_PWM_OUT_EN |= CH_MASK;
	} else {
		R8_PWM_OUT_EN &= (uint8_t)~CH_MASK;
		/* The pins fall back to their GPIO latches, which power.c holds
		 * low: gates off, LEDs off, and the state sleep expects. */
		funDigitalWrite(PIN_FLASHLIGHT_A, FUN_LOW);
		funDigitalWrite(PIN_FLASHLIGHT_B, FUN_LOW);
	}
	on = enable ? 1 : 0;
}

void flashlight_init(void)
{
	funPinMode(PIN_FLASHLIGHT_A, GPIO_CFGLR_OUT_2Mhz_PP);
	funPinMode(PIN_FLASHLIGHT_B, GPIO_CFGLR_OUT_2Mhz_PP);
	funDigitalWrite(PIN_FLASHLIGHT_A, FUN_LOW);
	funDigitalWrite(PIN_FLASHLIGHT_B, FUN_LOW);

	R8_PWM_CLOCK_DIV = PWM_DIV;
	R8_PWM_CONFIG    = 0;                      /* 8-bit, 256-clock cycle */
	R8_PWM_POLAR     = (uint8_t)((R8_PWM_POLAR & ~PWM6_BIT) | PWM8_BIT);
	set_level(FLASHLIGHT_PCT);
	drive(0);
}

/*
 * The decoder reports a double press BTN_MULTITAP_MS after the second
 * release, with the pad up and the momentary light already off -- so this
 * cannot ask whether the light is on. What it does ask is whether the first
 * of those two presses was the one that switched a latch off: the user
 * tapped twice to turn it off, and the decode must not switch it straight
 * back on.
 */
void flashlight_latch(uint32_t now)
{
	if (!FOB_ENABLE_FLASHLIGHT)
		return;
	if (ever_unlatched && (uint32_t)(now - t_unlatched) < FLASHLIGHT_RELATCH_GUARD_MS)
		return;
	if (!latched)
		set_level(FLASHLIGHT_PCT); /* every latch starts at the handout level */
	latched   = 1;
	t_latched = now;
}

/* One brightness step, `dir` +1 or -1, clamped to the step..cap range. */
static void step(int dir)
{
	int pct = (int)level_pct + dir * FLASHLIGHT_STEP_PCT;

	if (pct < FLASHLIGHT_STEP_PCT)
		pct = FLASHLIGHT_STEP_PCT;
	if (pct > FLASHLIGHT_MAX_PCT)
		pct = FLASHLIGHT_MAX_PCT;
	set_level((uint8_t)pct);
}

void flashlight_tick(uint8_t pressed_mask, int batt_low, uint32_t now)
{
	if (!FOB_ENABLE_FLASHLIGHT)
		return;

	uint8_t pressed = (pressed_mask >> FLASHLIGHT_BUTTON) & 1u;

	if (pressed && !was_pressed) {
		if (latched) {
			/* Any press while latched is "off", and this press must
			 * not relight as a momentary. */
			latched        = 0;
			hold_off       = 1;
			t_unlatched    = now;
			ever_unlatched = 1;
		}
	}
	if (!pressed)
		hold_off = 0;
	was_pressed = pressed;

	/*
	 * While latched, SW2 and SW3 are the dimmer. Acting on the press edge
	 * here, rather than on the decoded event, makes the step land the
	 * moment the pad is touched instead of a multi-tap window later. The
	 * pad is marked as taken so the main loop keeps its event off the air.
	 */
	static uint8_t was_steps;
	uint8_t steps = pressed_mask & ((1u << FLASHLIGHT_DIM_BUTTON) |
	                                (1u << FLASHLIGHT_BRIGHT_BUTTON));
	uint8_t edges = steps & (uint8_t)~was_steps;
	was_steps = steps;
	if (latched && edges) {
		if (edges & (1u << FLASHLIGHT_DIM_BUTTON))
			step(-1);
		if (edges & (1u << FLASHLIGHT_BRIGHT_BUTTON))
			step(+1);
		took_mask |= edges;
	}

	if (latched &&
	    (uint32_t)(now - t_latched) >= FLASHLIGHT_LATCH_MAX_MS)
		latched = 0;

	if (batt_low)
		latched = 0; /* a latch cannot outlast the battery */

	int want = (latched || (pressed && !hold_off)) && !batt_low;

	if (want != on)
		drive(want);
	else if (!latched && !on)
		level_pct = FLASHLIGHT_PCT; /* momentary is always the handout level */
}

int flashlight_took_press(uint8_t button)
{
	uint8_t bit = (uint8_t)(1u << button);
	int     took = (took_mask & bit) != 0;

	took_mask &= (uint8_t)~bit;
	return took;
}

int flashlight_on(void)
{
	return on;
}

uint8_t flashlight_level_pct(void)
{
	return on ? level_pct : 0;
}
