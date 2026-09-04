/* Host stand-in for ch32fun's low-power helpers, so power.c can be compiled
 * and its register writes asserted on. All no-ops; the tests care about what
 * power.c itself writes, not what these would do on silicon.
 *
 * LowPowerSleepWFI captures the state that matters *at the moment of sleep*,
 * which is the only point at which SysTick being masked has any meaning. */
#ifndef LEFOB_TEST_LOWPOWER_H
#define LEFOB_TEST_LOWPOWER_H

extern unsigned g_last_power_plan;
extern unsigned g_systick_ctlr_at_sleep;

static inline void DCDCEnable(void) {}
static inline void LSIEnable(void) {}
static inline void RTC_init(void) {}
static inline void SleepInit(void) {}
static inline void LowPowerSleepWFI(unsigned power_plan)
{
	g_last_power_plan = power_plan;
	g_systick_ctlr_at_sleep = SysTick->CTLR;
}

#endif
