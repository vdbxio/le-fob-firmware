/*
 * Le'Fob — CH592X BTHome v2 button fob.
 *
 * Four pads broadcast BTHome button events (press / double / triple / long /
 * long-double / long-triple / hold) and light the WS2812 sitting under the
 * pad that fired.
 *
 * Shape of the thing:
 *
 *   GPIO edge interrupt  ->  wake + "something happened" flag   (buttons.c)
 *   SysTick @ 1 kHz      ->  tick flag                          (this file)
 *   main loop            ->  debounce, gestures, LEDs, radio
 *
 * Nothing but flag-setting happens in an ISR. The gesture decoder, the
 * WS2812 bit-bang and the radio all run from the main loop, which keeps
 * interrupt latency short and means the LED refresh (which runs with
 * interrupts disabled) can never pre-empt a button edge.
 *
 * The interrupt plumbing exists mainly for sleep: once FOB_SLEEP_ENABLE is
 * turned on, the loop below parks in LowPower() whenever nothing is in
 * flight and a pad edge brings it straight back.
 */

#include "ch32fun.h"
#include <stdio.h>

#include "board.h"
#include "bthome.h"
#include "buttons.h"
#include "led_anim.h"
#include "ble_adv.h"

#if FOB_SLEEP_ENABLE
#include "ch5xx_lowpower.h"
#endif

/* Milliseconds since boot. Read by the main loop, written only by the
 * SysTick ISR. */
volatile uint32_t g_millis;

/* Set by the SysTick ISR, cleared by the main loop. */
static volatile uint8_t g_tick;

#define SYSTICK_PER_MS (FUNCONF_SYSTEM_CORE_CLOCK / 1000)

void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void)
{
	/*
	 * Advance the compare register by a fixed step rather than reloading
	 * from the current count. If this interrupt is ever late -- and it will
	 * be, by up to ~120 us whenever the WS2812 chain is refreshed with
	 * interrupts off -- the lateness does not accumulate into the clock.
	 */
	SysTick->CMP += SYSTICK_PER_MS;
	SysTick->SR   = 0;

	g_millis++;
	g_tick = 1;
}

static void systick_init(void)
{
	SysTick->CTLR = 0;
	SysTick->CNT  = 0;
	SysTick->CMP  = SYSTICK_PER_MS;
	SysTick->CTLR = SYSTICK_CTLR_STE   | /* enable */
	                SYSTICK_CTLR_STIE  | /* interrupt */
	                SYSTICK_CTLR_STCLK;  /* HCLK/1, matching funconfig.h */

	NVIC_EnableIRQ(SysTick_IRQn);
}

static void banner(void)
{
	const uint8_t *m = ble_adv_mac();

	printf("\n.~ Le'Fob — CH592X BTHome v2 ~.\n");

	/* Printed most-significant byte first, the way a scanner shows it. */
	printf("addr   %02X:%02X:%02X:%02X:%02X:%02X (%s)\n",
	       m[5], m[4], m[3], m[2], m[1], m[0],
	       ble_adv_mac_is_random() ? "random static" : "factory");
	printf("name   %s\n", BTHOME_NAME);
	printf("pads   SW1 PB22 | SW2 PB4 | SW3 PB23 | SW4 PA9\n");
	printf("pixels %d on PB7 (PIXEL0)\n", WS2812_COUNT);

	/*
	 * PB23 leaves the factory as RST#. Until the option bit is cleared,
	 * pressing SW3 resets the chip instead of registering a press -- which
	 * looks exactly like a crash if you do not know to expect it.
	 */
	printf("note   SW3/PB23 needs 'make disable-reset' once per board,\n");
	printf("       otherwise it resets instead of reporting.\n\n");
}

int main(void)
{
	SystemInit();
	funGpioInitAll(); /* no-op on ch5xx, kept for symmetry */

	systick_init();
	buttons_init();
	led_anim_init();
	ble_adv_init();

	banner();

	/* Last time anything happened. Only the sleep path reads this, so it
	 * is deliberately unused in a FOB_SLEEP_ENABLE=0 build. */
	__attribute__((unused)) uint32_t t_active = g_millis;

	while (1) {
		if (g_tick) {
			g_tick = 0;

			uint32_t now = g_millis;

			buttons_tick(now);

			uint8_t button, event;

			while (buttons_pop_event(&button, &event)) {
				printf("btn%d %s\n", button + 1,
				       buttons_event_name(event));

				led_anim_trigger(button, event, now);
				ble_adv_send_event(button, event);
			}

			/*
			 * Feed the two state-tracking LED layers before
			 * rendering. Order matters only in that both must be
			 * current for this frame.
			 */
			led_anim_set_pressed(buttons_pressed_mask());
			led_anim_set_tx(ble_adv_tx_button());

			led_anim_tick(now);
			ble_adv_tick(now);

			if (buttons_busy() || led_anim_busy() || ble_adv_busy())
				t_active = now;
		}

#if FOB_SLEEP_ENABLE
		/*
		 * Sleep only once everything has settled: no pad down, no
		 * gesture mid-decode, no animation, no burst outstanding, plus
		 * a short linger so a user between taps of a double-press is
		 * never cut off.
		 */
		if ((uint32_t)(g_millis - t_active) >= FOB_IDLE_LINGER_MS) {
			led_anim_all_off();

			/*
			 * No RTC alarm: a button edge is the only thing worth
			 * waking for. RB_SLP_GPIO_WAKE was armed in buttons_init().
			 */
			LowPowerSleepWFI(RB_PWR_RAM2K | RB_PWR_RAMX | RB_PWR_EXTEND);

			ble_adv_resume(); /* radio comes back up scrambled */
			DCDCEnable();     /* and the DC-DC is off after sleep */

			buttons_take_wake();
			t_active = g_millis;
		} else {
			__WFI();
		}
#else
		/* Idle until the next interrupt rather than spinning; this is
		 * the same wake path sleep uses, minus the power gating. */
		__WFI();
#endif
	}
}
