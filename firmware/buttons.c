#include "ch32fun.h"
#include "board.h"
#include "buttons.h"
#include "bthome.h"

/*
 * ---------------------------------------------------------------------------
 * Hardware layer
 * ---------------------------------------------------------------------------
 *
 * Two of the four pads need care.
 *
 * PB22 and PB23 have no bits of their own in the 16-bit R16_PB_INT_*
 * registers. WCH routes them through interrupt lines INT24/INT25, which
 * appear as PB interrupt bits 8 and 9 -- bits that are otherwise unused on
 * this package, because CH592X has no PB8/PB9 pads. Two things follow:
 *
 *   1. R16_PIN_ALTERNATE must have RB_PIN_INTX set, or bits 8/9 listen to
 *      nothing at all on a QFN-32 part.
 *   2. The interrupt registers (EN / MODE / IF) are addressed by the
 *      *remapped* bit 8 or 9, while the pad registers (OUT / CLR / PIN) are
 *      addressed by the real bit 22 or 23. Mixing them up silently produces
 *      a button that never fires.
 *
 * This mirrors GPIOB_ITModeCfg() in WCH's own CH59x_gpio.c, which computes
 * the same remap as `pin | ((pin & (Pin_22|Pin_23)) >> 14)`.
 *
 * Edge polarity on CH59x is not a field in the interrupt registers: with
 * R16_Px_INT_MODE set to edge mode, the pin's *output* latch selects which
 * edge. Clearing it (via R32_Px_CLR) selects falling. Since the pads are
 * configured as inputs, the output latch drives nothing, so it is free to
 * be used this way -- and funPinMode(GPIO_ModeIN_PU) leaves it alone.
 */

typedef struct {
	uint32_t pin;      /* ch32fun pin token */
	uint32_t port_bit; /* real bit in R32_Px_OUT / R32_Px_CLR / R32_Px_PIN */
	uint16_t int_bit;  /* bit in R16_Px_INT_EN / _MODE / _IF (remapped) */
	uint8_t  is_pb;
} btn_hw_t;

static const btn_hw_t btn_hw[NUM_BUTTONS] = {
	/* SW1, PB22 -> interrupt bit 8  */ { PIN_BTN1, 1u << 22, 1u << 8, 1 },
	/* SW2, PB4                      */ { PIN_BTN2, 1u << 4,  1u << 4, 1 },
	/* SW3, PB23 -> interrupt bit 9  */ { PIN_BTN3, 1u << 23, 1u << 9, 1 },
	/* SW4, PA9                      */ { PIN_BTN4, 1u << 9,  1u << 9, 0 },
};

static volatile uint8_t g_wake_flag;

/* Active low: the pad reads 0 when the switch shorts it to ground. */
static inline int btn_raw_pressed(const btn_hw_t *hw)
{
	uint32_t pins = hw->is_pb ? R32_PB_PIN : R32_PA_PIN;
	return (pins & hw->port_bit) ? 0 : 1;
}

/*
 * ---------------------------------------------------------------------------
 * Gesture decoding
 * ---------------------------------------------------------------------------
 *
 * Per pad:
 *   - taps accumulate on each release and are resolved once BTN_MULTITAP_MS
 *     passes with no new press, giving press / double / triple.
 *   - a press held past BTN_LONG_MS emits long_press straight away, without
 *     waiting for the release, so the fob responds while still in hand. Any
 *     taps banked before that press upgrade it to long_double / long_triple,
 *     which is what those BTHome codes mean.
 *   - holding further, past BTN_HOLD_MS, emits hold_press.
 *   - a release that follows a long or hold does not count as a tap; the
 *     gesture has already been reported.
 */

typedef struct {
	uint8_t  level;      /* debounced state, 1 = pressed */
	uint8_t  raw;        /* last raw sample */
	uint8_t  taps;       /* releases banked in the current burst */
	uint8_t  long_sent;  /* long_press already emitted for this hold */
	uint8_t  hold_sent;  /* hold_press already emitted for this hold */
	uint8_t  settling;   /* raw disagrees with level, waiting it out */
	uint32_t t_raw;      /* when raw last changed */
	uint32_t t_press;    /* when the current press was accepted */
	uint32_t t_release;  /* when the last release was accepted */
} btn_state_t;

static btn_state_t btn[NUM_BUTTONS];

/* Small ring buffer so a fast double-tap on two pads cannot drop a gesture
 * while the radio is busy with the previous one. */
#define EVQ_LEN 8
static struct {
	uint8_t button;
	uint8_t event;
} evq[EVQ_LEN];
static uint8_t evq_head, evq_tail;

static void evq_push(uint8_t button, uint8_t event)
{
	uint8_t next = (uint8_t)((evq_head + 1) % EVQ_LEN);

	if (next == evq_tail)
		return; /* full: drop the newest rather than corrupt the queue */

	evq[evq_head].button = button;
	evq[evq_head].event  = event;
	evq_head             = next;
}

int buttons_pop_event(uint8_t *button, uint8_t *event)
{
	if (evq_tail == evq_head)
		return 0;

	*button  = evq[evq_tail].button;
	*event   = evq[evq_tail].event;
	evq_tail = (uint8_t)((evq_tail + 1) % EVQ_LEN);
	return 1;
}

/* Map a count of banked taps onto the matching BTHome "long" variant. */
static uint8_t long_event_for(uint8_t banked_taps)
{
	if (banked_taps >= 2)
		return BTHOME_EVT_LONG_TRIPLE_PRESS;
	if (banked_taps == 1)
		return BTHOME_EVT_LONG_DOUBLE_PRESS;
	return BTHOME_EVT_LONG_PRESS;
}

static uint8_t tap_event_for(uint8_t taps)
{
	if (taps >= 3)
		return BTHOME_EVT_TRIPLE_PRESS;
	if (taps == 2)
		return BTHOME_EVT_DOUBLE_PRESS;
	return BTHOME_EVT_PRESS;
}

void buttons_tick(uint32_t now)
{
	for (int i = 0; i < NUM_BUTTONS; i++) {
		btn_state_t    *s  = &btn[i];
		const btn_hw_t *hw = &btn_hw[i];

		uint8_t raw = (uint8_t)btn_raw_pressed(hw);

		/*
		 * Time-based debounce rather than a sample counter: the tick can
		 * be delayed by a WS2812 refresh or a BLE burst, and counting
		 * samples would stretch the debounce window by however long that
		 * took.
		 */
		if (raw != s->raw) {
			s->raw      = raw;
			s->t_raw    = now;
			s->settling = 1;
		} else if (s->settling && raw != s->level &&
		           (uint32_t)(now - s->t_raw) >= BTN_DEBOUNCE_MS) {
			s->settling = 0;
			s->level    = raw;

			if (raw) {
				/* Accepted press. */
				s->t_press   = now;
				s->long_sent = 0;
				s->hold_sent = 0;
			} else {
				/* Accepted release. */
				if (s->long_sent || s->hold_sent) {
					/* The hold already spoke for this press. */
					s->taps = 0;
				} else {
					if (s->taps < 3)
						s->taps++;
					s->t_release = now;
				}
			}
		} else if (raw == s->level) {
			s->settling = 0;
		}

		if (s->level) {
			/* Held down: check the long and hold thresholds. */
			if (!s->long_sent &&
			    (uint32_t)(now - s->t_press) >= BTN_LONG_MS) {
				s->long_sent = 1;
				evq_push((uint8_t)i, long_event_for(s->taps));
				s->taps = 0;
			}
			if (s->long_sent && !s->hold_sent &&
			    (uint32_t)(now - s->t_press) >= BTN_HOLD_MS) {
				s->hold_sent = 1;
				evq_push((uint8_t)i, BTHOME_EVT_HOLD_PRESS);
			}
		} else if (s->taps &&
		           (uint32_t)(now - s->t_release) >= BTN_MULTITAP_MS) {
			/* Released long enough that no further tap is coming. */
			evq_push((uint8_t)i, tap_event_for(s->taps));
			s->taps = 0;
		}
	}
}

uint8_t buttons_pressed_mask(void)
{
	uint8_t mask = 0;

	for (int i = 0; i < NUM_BUTTONS; i++) {
		if (btn[i].level)
			mask |= (uint8_t)(1u << i);
	}
	return mask;
}

int buttons_busy(void)
{
	if (evq_tail != evq_head)
		return 1;

	for (int i = 0; i < NUM_BUTTONS; i++) {
		/* Down, mid-bounce, or banking taps towards a multi-tap. */
		if (btn[i].level || btn[i].settling || btn[i].taps)
			return 1;
	}
	return 0;
}

uint8_t buttons_take_wake(void)
{
	uint8_t w = g_wake_flag;

	g_wake_flag = 0;
	return w;
}

const char *buttons_event_name(uint8_t event)
{
	switch (event) {
	case BTHOME_EVT_PRESS:             return "press";
	case BTHOME_EVT_DOUBLE_PRESS:      return "double";
	case BTHOME_EVT_TRIPLE_PRESS:      return "triple";
	case BTHOME_EVT_LONG_PRESS:        return "long";
	case BTHOME_EVT_LONG_DOUBLE_PRESS: return "long_double";
	case BTHOME_EVT_LONG_TRIPLE_PRESS: return "long_triple";
	case BTHOME_EVT_HOLD_PRESS:        return "hold";
	default:                           return "none";
	}
}

/*
 * ---------------------------------------------------------------------------
 * Interrupts
 * ---------------------------------------------------------------------------
 */

void GPIOA_IRQHandler(void) __attribute__((interrupt));
void GPIOA_IRQHandler(void)
{
	uint16_t flags = R16_PA_INT_IF;

	R16_PA_INT_IF = flags; /* write 1 to clear */
	g_wake_flag   = 1;
}

void GPIOB_IRQHandler(void) __attribute__((interrupt));
void GPIOB_IRQHandler(void)
{
	uint16_t flags = R16_PB_INT_IF;

	R16_PB_INT_IF = flags;
	g_wake_flag   = 1;
}

void buttons_init(void)
{
	/*
	 * Route interrupt lines INT24/INT25 to the PB22/PB23 pads. Without
	 * this they sit on PB8/PB9, which do not exist on the QFN-32 package,
	 * and SW1 and SW3 would never raise an interrupt.
	 */
	R16_PIN_ALTERNATE |= RB_PIN_INTX;

	for (int i = 0; i < NUM_BUTTONS; i++) {
		const btn_hw_t *hw = &btn_hw[i];

		funPinMode(hw->pin, GPIO_CFGLR_IN_PUPD);

		if (hw->is_pb) {
			R16_PB_INT_MODE |= hw->int_bit;  /* edge, not level */
			R32_PB_CLR       = hw->port_bit; /* ... falling edge */
			R16_PB_INT_IF    = hw->int_bit;  /* discard anything stale */
			R16_PB_INT_EN   |= hw->int_bit;
		} else {
			R16_PA_INT_MODE |= hw->int_bit;
			R32_PA_CLR       = hw->port_bit;
			R16_PA_INT_IF    = hw->int_bit;
			R16_PA_INT_EN   |= hw->int_bit;
		}
	}

	/* Seed the debounced state from the real pin levels, so a button held
	 * across reset is not reported as a fresh press. */
	for (int i = 0; i < NUM_BUTTONS; i++) {
		btn[i].raw   = (uint8_t)btn_raw_pressed(&btn_hw[i]);
		btn[i].level = btn[i].raw;
	}

	/*
	 * Let a GPIO edge bring the chip out of deep sleep. Armed even when
	 * FOB_SLEEP_ENABLE is 0 -- it costs nothing while awake, and it means
	 * the wake path is exercised by the same code that ships.
	 */
	SYS_SAFE_ACCESS(
		R8_SLP_WAKE_CTRL |= RB_SLP_GPIO_WAKE;
	);

	NVIC_EnableIRQ(GPIOA_IRQn);
	NVIC_EnableIRQ(GPIOB_IRQn);
}
