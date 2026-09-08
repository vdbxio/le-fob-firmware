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
#include "ws2812.h"
#include "ble_adv.h"
#include "power.h"
#include "charger.h"
#include "chirp.h"
#include "status.h"
#include "isp.h"

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
	printf("radio  tx %+d dBm, %d rounds over %d ms (every %d ms)\n",
	       BLE_TX_POWER_DBM, ADV_BURST_ROUNDS, ADV_BURST_MS,
	       ADV_ROUND_INTERVAL_MS);
	printf("isp    hold SW1 %d ms%s\n", ISP_HOLD_MS,
	       FOB_ISP_REQUIRE_USB ? " (USB must be present)" : "");
	{
		/* Crystal load trim as the chip is actually running it: the
		 * register encodes (pF - 10) / 2 in bits 6:4. */
		uint8_t tune = R8_XT32M_TUNE;
		printf("xtal   tune 0x%02X = %d pF load, bias %d%s\n", tune,
		       10 + 2 * ((tune & RB_XT32M_C_LOAD) >> 4),
		       tune & RB_XT32M_I_BIAS,
		       FOB_HSE_CAP_PF ? " (set by FOB_HSE_CAP_PF)" : " (reset value)");
	}
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

/* Every pad the same colour, pushed straight to the chain, bypassing the
 * animation engine: this is the last thing the app draws. */
static void pads_solid(uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t grb[WS2812_COUNT * 3];

	for (int i = 0; i < WS2812_COUNT; i++) {
		grb[i * 3 + 0] = g;
		grb[i * 3 + 1] = r;
		grb[i * 3 + 2] = b;
	}
	ws2812_show(grb, WS2812_COUNT);
}

/*
 * The SW1 hold has run its course: hand the chip to the ROM bootloader.
 *
 * jump_isprom() copies the ROM into RAM, patches out its own BOOT-pin
 * check, and mret's into it, so from here the chip is in ISP mode exactly
 * as if it had been reset with SW1 down -- which is the thing a battery
 * stops USB from doing (see isp.h).
 */
static void isp_enter(void)
{
	const uint8_t v = (uint8_t)((255 * PAD_LED_BRIGHTNESS_PCT) / 100);

#if FOB_ISP_REQUIRE_USB && FOB_ENABLE_CHARGER
	charger_status_t cs;

	if (charger_read(&cs) && cs.valid && !cs.power_good) {
		printf("isp    refused: charger reports no USB\n");
		for (int i = 0; i < 3; i++) {
			pads_solid(v, 0, 0);
			Delay_Ms(80);
			pads_solid(0, 0, 0);
			Delay_Ms(80);
		}
		led_anim_invalidate();
		return;
	}
#endif

	printf("isp    jumping to bootloader\n");
	pads_solid(v, v, v);
	Delay_Ms(150);
	pads_solid(0, 0, 0);

	/* Nothing of ours may fire once the bootloader has the vector table. */
	SysTick->CTLR = 0;
	NVIC_DisableIRQ(SysTick_IRQn);
	NVIC_DisableIRQ(GPIOA_IRQn);
	NVIC_DisableIRQ(GPIOB_IRQn);
	NVIC_DisableIRQ(RTC_IRQn);

	jump_isprom();
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

			/* SW1 held alone long enough -> bootloader (see isp.h). */
			if (isp_hold_tick(buttons_pressed_mask(), now))
				isp_enter(); /* returns only if it refused */
			led_anim_set_arming(isp_hold_progress());

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
			if (FOB_ENABLE_CHARGER_INT && buttons_take_charger_int()) {
				if (power_rtc_elapsed_ms(t_last_interim, rtc) >=
				    CHARGER_INT_MIN_GAP_MS) {
					send           = 1;
					t_last_interim = rtc;
					printf("chirp (charger)\n");
				}
			}

			if (send) {
				if (!FOB_DISABLE_RADIO)
					ble_adv_send_heartbeat(status_text());
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
