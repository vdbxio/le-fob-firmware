/*
 * Host-side check of the sleep/wake register configuration in
 * firmware/power.c.
 *
 * This exists because arming the wake source partially is a silent failure:
 * the fob sleeps beautifully and simply never comes back, with no way to
 * tell from the outside which of several bits was missed. That happened --
 * RB_SLP_GPIO_WAKE was set and RB_WAKE_EV_MODE was not, so a button edge,
 * being a short pulse, was never seen.
 *
 * The registers are plain variables here (tests/stub/ch32fun.h), so the real
 * power.c can be run and its writes asserted.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/power.h"

/* Backing storage for the stubbed registers. */
uint8_t  g_fake_rom[6];
unsigned char  R8_SLP_WAKE_CTRL;
unsigned char  R8_SLP_POWER_CTRL;
unsigned char  R8_RTC_FLAG_CTRL;
unsigned short R16_RTC_CNT_LSI;
unsigned short R16_RTC_CNT_DIV1;
unsigned g_last_power_plan;
unsigned g_systick_ctlr_at_sleep;

static SysTick_Type systick_storage;
SysTick_Type *SysTick = &systick_storage;

/* power.c calls these on wake; not under test here. */
void ble_adv_resume(void) {}

/* Defined in power.c; declared here because it is an interrupt handler and
 * so has no place in a public header. */
void RTC_IRQHandler(void);

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

int main(void)
{
	printf("\n== GPIO wake arming ==\n");

	R8_SLP_WAKE_CTRL  = 0;
	R8_SLP_POWER_CTRL = 0xFF; /* start dirty, so clearing is visible */

	power_arm_gpio_wake();

	check(R8_SLP_WAKE_CTRL & RB_SLP_GPIO_WAKE, "GPIO is enabled as a wake source");

	/*
	 * The bit whose absence cost a debugging round. A button edge is a
	 * short pulse; with this clear the wake logic waits for an event that
	 * stays valid, and the edge is never seen.
	 */
	check(R8_SLP_WAKE_CTRL & RB_WAKE_EV_MODE,
	      "wake events are treated as short pulses");

	/* WCH's default: 01, short delay, 520 cycles + TSUHSE. Anything else
	 * either wakes too slowly or before the HSE has restarted. */
	check((R8_SLP_POWER_CTRL & RB_WAKE_DLY_MOD) == 0x01,
	      "wake delay is the short-but-safe setting");

	printf("\n== sleep power plan ==\n");

	power_sleep();

	check(g_last_power_plan & RB_PWR_MUST_0010,
	      "plan includes the must-write-0010 field");
	check(g_last_power_plan & RB_PWR_RAM2K, "retention RAM kept powered");
	check(g_last_power_plan & RB_PWR_RAMX, "main RAM kept powered");

	printf("\n== SysTick is masked across sleep ==\n");

	/*
	 * WFI returns on any pending interrupt. SysTick at 1 kHz will always
	 * be pending within a millisecond, so leaving it armed turns deep
	 * sleep into a thrash that costs more than idling -- which is exactly
	 * what the first PPK2 capture showed. The stub records CTLR at the
	 * moment of sleep, since that is the only instant it matters.
	 *
	 * Going in with it set also covers the opposite mistake: masking it
	 * and never restoring would stop the main loop dead.
	 */
	systick_storage.CTLR = SYSTICK_CTLR_STIE;
	power_sleep();

	check(!(g_systick_ctlr_at_sleep & SYSTICK_CTLR_STIE),
	      "SysTick interrupt is masked at the moment of sleep");
	check(systick_storage.CTLR & SYSTICK_CTLR_STIE,
	      "SysTick interrupt is restored after waking");
	check(systick_storage.CMP > systick_storage.CNT,
	      "next tick is scheduled ahead of the counter");

	printf("\n== RTC handler clears its flag ==\n");

	/*
	 * SleepInit() enables RTC_IRQn and ch32fun's unhandled-interrupt
	 * default spins forever, so this handler existing at all is the fix.
	 * It must also clear the flag, or it would re-enter immediately and
	 * keep WFI from ever sleeping.
	 */
	R8_RTC_FLAG_CTRL = 0;
	RTC_IRQHandler();
	check(R8_RTC_FLAG_CTRL & RB_RTC_TRIG_CLR, "trigger flag is cleared");

	printf("\n== RTC elapsed time survives the daily wrap ==\n");

	check(power_rtc_elapsed_ms(0, 32768) == 1000, "one second of ticks");
	check(power_rtc_elapsed_ms(RTC_MAX_COUNT - 16384, 16384) == 1000,
	      "a second spanning the wrap point");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
