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
#include "power.h"
#include "charger.h"
#include "chirp.h"

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

	if (!FOB_ENABLE_CHARGER) {
		printf("chgr   driver disabled (FOB_ENABLE_CHARGER=0)\n");
	} else if (charger_present()) {
		charger_status_t chg;

		if (charger_read(&chg))
			printf("chgr   AW32001 @0x%02X: %s, VBUS %s%s\n",
			       CHARGER_I2C_ADDR, charger_state_name(chg.state),
			       chg.power_good ? "present" : "absent",
			       chg.faults ? ", FAULTS" : "");
		else
			printf("chgr   AW32001 @0x%02X found, status read failed\n",
			       CHARGER_I2C_ADDR);
	} else {
		printf("chgr   not responding at 0x%02X (bus pull-ups?)\n",
		       CHARGER_I2C_ADDR);
	}

	if (FOB_DISABLE_RADIO)
		printf("RADIO  DISABLED (FOB_DISABLE_RADIO=1) - LED diagnostic build\n");

	/* Which build this is, so a power capture cannot be misattributed. */
	printf("sleep  %s\n",
	       FOB_SLEEP_ENABLE ? "deep sleep enabled"
	                        : "DISABLED - idles in WFI, expect ~1 mA from the MCU");

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

	/* First: clocks, regulator, and quiescing everything nothing else
	 * owns. Drivers below configure their own pins over the top. */
	power_init();

	systick_init();
	buttons_init();
	led_anim_init();

	if (!FOB_DISABLE_RADIO)
		ble_adv_init();

	if (FOB_ENABLE_CHARGER)
		charger_init();

	chirp_init(power_rtc_ticks());

	banner();

	/* Last time anything happened, and how long to stay up after it.
	 * Only the sleep path reads these, so they are deliberately unused in
	 * a FOB_SLEEP_ENABLE=0 build. */
	__attribute__((unused)) uint32_t t_active = g_millis;
	__attribute__((unused)) uint32_t linger   = FOB_IDLE_LINGER_MS;

	/* RTC time of the last interim (charger-triggered) chirp, for the
	 * rate limit. Starts far enough back that the first one is allowed. */
	uint32_t t_last_interim = power_rtc_ticks() - 32768u * 60u;

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

				if (!FOB_DISABLE_RADIO)
					ble_adv_send_event(button, event);
			}

			/*
			 * Feed the two state-tracking LED layers before
			 * rendering. Order matters only in that both must be
			 * current for this frame.
			 */
			led_anim_set_pressed(buttons_pressed_mask());
			led_anim_set_tx(FOB_DISABLE_RADIO ? -1 : ble_adv_tx_button());

			led_anim_tick(now);

			if (!FOB_DISABLE_RADIO)
				ble_adv_tick(now);

			/*
			 * The chirp is scheduled purely by RTC time. The RTC alarm
			 * exists only to wake the chip; whether a chirp is due is
			 * decided here, so a button wake that lands just before the
			 * alarm sends it too, and an alarm that fires while awake
			 * costs nothing extra.
			 */
			uint32_t rtc = power_rtc_ticks();
			int      send = 0;

			if (CHIRP_INTERVAL_MS && chirp_due(rtc)) {
				send = 1;
				printf("chirp\n");
			}

			/*
			 * Charger INT: plug, unplug, state change, fault. Worth an
			 * interim chirp so the receiver hears about it now rather
			 * than at the next scheduled one -- but INT can fire several
			 * times in quick succession as the charger steps through
			 * states, so anything inside CHARGER_INT_MIN_GAP_MS rides
			 * along with the chirp already sent.
			 */
			if (buttons_take_charger_int()) {
				if (power_rtc_elapsed_ms(t_last_interim, rtc) >=
				    CHARGER_INT_MIN_GAP_MS) {
					send           = 1;
					t_last_interim = rtc;
					printf("chirp (charger)\n");
				}
			}

			if (send) {
				if (!FOB_DISABLE_RADIO)
					ble_adv_send_heartbeat();
				/* An interim chirp counts: restart the schedule. */
				chirp_mark_sent(rtc);
			}

			if (buttons_busy()) {
				/* A human is involved: use the human linger. */
				linger = FOB_IDLE_LINGER_MS;
			}
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
		if ((uint32_t)(g_millis - t_active) >= linger) {
			led_anim_all_off();

			uint32_t slept_from = power_rtc_ticks();

			/*
			 * Sleep until the next chirp is due, or until a button. A
			 * zero interval disables timed wakes entirely.
			 */
			power_sleep_for_ms(CHIRP_INTERVAL_MS
			                   ? chirp_ms_until(slept_from) : 0);

			/*
			 * g_millis did not advance while asleep -- SysTick is
			 * clocked from HCLK, which stops. Anything that needs real
			 * elapsed time has to come from the RTC, which does not.
			 */
			uint32_t slept_ms =
				power_rtc_elapsed_ms(slept_from, power_rtc_ticks());

			uint8_t by_button = buttons_take_wake();
			uint8_t by_rtc    = power_take_rtc_wake();

			printf("wake after %lums (%s)\n", (unsigned long)slept_ms,
			       by_button ? "button" : by_rtc ? "rtc" : "charger/other");

			/* The charger flag is deliberately NOT taken here: it is
			 * consumed in the tick above, where the chirp is sent. */

			/*
			 * A chirp wake has no human in it. Stay up only long enough
			 * for the burst; a button press during that window switches
			 * back to the human linger above.
			 */
			linger   = by_button ? FOB_IDLE_LINGER_MS : FOB_CHIRP_LINGER_MS;
			t_active = g_millis;
		} else {
			power_idle();
		}
#else
		/* Idle until the next interrupt rather than spinning; this is
		 * the same wake path sleep uses, minus the power gating. */
		power_idle();
#endif
	}
}
