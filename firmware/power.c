#include "ch32fun.h"
#include "board.h"
#include "power.h"
#include "ble_adv.h"
#include "led_anim.h"
#include "buttons.h"
#include "ws2812.h"

#include "ch5xx_lowpower.h"

/*
 * Pins this firmware does not otherwise touch.
 *
 * A CMOS input left floating sits near its switching threshold and draws
 * crossbar current, which is the usual reason a chip that should sleep at
 * single-digit microamps sits at tens instead. That is what most of this
 * function is for.
 *
 * PB0 (PWM6) and PB6 (PWM8), the gates of the dual N-FET driving the white
 * LEDs and the IR emitter, are NOT such a case: the board fits 10k
 * pull-downs on both, so they are held off in hardware whatever this
 * firmware does, and they were never floating. Driving them low here is
 * belt-and-braces -- it states the intent, and it means a future flashlight
 * feature starts from a known output state rather than reconfiguring a pin
 * mid-flight. It fixes nothing on its own.
 *
 * PB10/PB11 are USB D-/D+ and are deliberately left alone: a pull-up on D+
 * is how a device signals attach, and this firmware has no USB stack to
 * answer with.
 */
void power_apply_pin_config(void)
{
	/* Gates of the dual N-FET. Board pull-downs already hold these off;
	 * driving them low is explicitness, not a fix. */
	funPinMode(PB0, GPIO_CFGLR_OUT_2Mhz_PP);
	funDigitalWrite(PB0, FUN_LOW);
	funPinMode(PB6, GPIO_CFGLR_OUT_2Mhz_PP);
	funDigitalWrite(PB6, FUN_LOW);

	/* PIXEL1, the CON1 expansion header. Idle low, as a WS2812 data line
	 * should be, in case a strip is attached. */
	funPinMode(PA14, GPIO_CFGLR_OUT_2Mhz_PP);
	funDigitalWrite(PA14, FUN_LOW);

	/*
	 * Castellations and unconnected analog pins: pull up so they are not
	 * left floating.
	 *
	 * See FOB_QUIESCE_PULLUPS in board.h for why this is worth being able
	 * to switch off -- a pull-up into a pin something else holds low costs
	 * 65-165 uA, and PA4 (the charger's open-drain INT) is the candidate.
	 */
	const GPIOModeTypeDef mode = FOB_QUIESCE_PULLUPS ? GPIO_CFGLR_IN_PUPD
	                                                 : GPIO_CFGLR_IN_FLOAT;

	funPinMode(PA4,  mode);   /* charger nIRQ, unused here */
	funPinMode(PA5,  mode);
	funPinMode(PA8,  mode);
	funPinMode(PA12, mode);   /* DISP_RST, OLED is DNP */
	funPinMode(PA13, mode);
	funPinMode(PA15, mode);
	funPinMode(PB12, mode);   /* UART1 RX */
	funPinMode(PB13, mode);   /* UART1 TX */

	/*
	 * I2C. On boards where R3/R4 are fitted these internal pull-ups just
	 * match the board ones; where they are missing, these are the only
	 * thing holding the bus idle high. The internal pull-up is weak -- IUP
	 * is 25/60/90 uA at 3.3 V, so 37-132 k -- which is usable for a short
	 * single-device bus only at a low bit rate. See docs/i2c-pullups.md.
	 */
	funPinMode(PB14, mode);
	funPinMode(PB15, mode);
}

/*
 * Arm GPIO as a wake source.
 *
 * Only RB_SLP_GPIO_WAKE. The two neighbouring bits are deliberately left at
 * their reset values, and an earlier version that set them was wrong:
 *
 *   RB_GPIO_WAKE_MODE (bit 7) selects edge or level. Reset is 0, level
 *   triggered, which is what these buttons want -- they are active low with
 *   pull-ups, so a press pulls the pin low and holds it there. The event is
 *   a level, not an edge.
 *
 *   RB_WAKE_EV_MODE (bit 6) is described by the datasheet as "wake-up event
 *   internal memory mode enable: 1: enable memory, support short-pulse event
 *   wake-up; 0: disable memory, the event should remain valid until wake
 *   up." A held button *is* an event that remains valid until wake, so the
 *   reset value of 0 is correct. Setting it enables a latch this firmware
 *   never clears, and a latch that is never cleared stops working after it
 *   has latched.
 *
 * The wake delay (RB_WAKE_DLY_MOD) is likewise left at reset, 00, the
 * longest setting at 3590 cycles + TSUHSE. Shortening it wakes sooner but
 * gives the 32 MHz crystal less time to restart, and for a fob that wakes on
 * a human finger the latency is worth nothing and the margin is worth
 * having.
 *
 * WCH's PWR_PeriphWakeUpCfg() sets both bits, which is what led to setting
 * them here. It is a general-purpose helper covering wake sources that are
 * genuinely pulses; for a level-held button it is the wrong shape.
 */
void power_arm_gpio_wake(void)
{
	SYS_SAFE_ACCESS(
		R8_SLP_WAKE_CTRL |= RB_SLP_GPIO_WAKE;
	);
}

void power_init(void)
{
	/*
	 * The internal DC-DC cuts active current to roughly 60% of running off
	 * the LDO (datasheet 5.1). Enable it before anything starts drawing.
	 */
	DCDCEnable();

#if FOB_HSE_CAP_PF
	/*
	 * Crystal load trim (see board.h). The register holds (pF - 10) / 2 in
	 * bits 6:4; the bias bits below it are left as they are. Applied
	 * before anything RF runs and never touched again -- the setting
	 * survives sleep with the rest of the always-on domain.
	 */
	_Static_assert(FOB_HSE_CAP_PF >= 10 && FOB_HSE_CAP_PF <= 24 &&
	               (FOB_HSE_CAP_PF % 2) == 0,
	               "FOB_HSE_CAP_PF must be an even value from 10 to 24");
	SYS_SAFE_ACCESS(
		R8_XT32M_TUNE = (uint8_t)((R8_XT32M_TUNE & ~RB_XT32M_C_LOAD) |
		                          (((FOB_HSE_CAP_PF - 10) / 2) << 4));
	);
#endif

	/*
	 * Run the 32 kHz domain from the internal LSI rather than the LSE
	 * crystal. X2 is fitted, so LSE is available and would be more
	 * accurate -- worth revisiting when the chirp interval wants to be
	 * precise -- but LSI starts faster and needs no crystal settling.
	 */
	LSIEnable();

	RTC_init();   /* zero the counters, enable the trigger */
	SleepInit();  /* arm RTC wake and its interrupt */

	power_apply_pin_config();
	power_arm_gpio_wake();
}

void power_idle(void)
{
	/* Belt and braces: never trust SLEEPDEEP to be clear on the way in. */
	NVIC->SCTLR &= ~(1 << 2);
	__WFI();
}

uint32_t power_rtc_ticks(void)
{
	return (uint32_t)R16_RTC_CNT_LSI |
	       ((uint32_t)R16_RTC_CNT_DIV1 << 16);
}

uint32_t power_rtc_elapsed_ms(uint32_t from, uint32_t to)
{
	/*
	 * The counter wraps at RTC_MAX_COUNT, which is one day's worth of
	 * 32768 Hz ticks and not a power of two -- so plain modular subtraction
	 * would be wrong and the wrap has to be handled explicitly.
	 */
	uint32_t ticks = (to >= from) ? (to - from)
	                              : (to + RTC_MAX_COUNT - from);

	return (uint32_t)(((uint64_t)ticks * 1000u) / 32768u);
}

/*
 * The RTC trigger interrupt.
 *
 * SleepInit() enables this interrupt, and ch32fun's unhandled-interrupt
 * default spins forever -- so without a handler here, the first RTC trigger
 * hangs the fob. RTC_init() arms a trigger at count 0 and the counter wraps
 * daily, so "the first trigger" is not hypothetical.
 *
 * Nothing needs doing beyond clearing the flag: this exists so a timed wake
 * returns to the main loop rather than into a spin, and so the flag cannot
 * sit pending and keep WFI from ever sleeping.
 */
static volatile uint8_t g_rtc_wake;

void RTC_IRQHandler(void) __attribute__((interrupt));
void RTC_IRQHandler(void)
{
	R8_RTC_FLAG_CTRL = RB_RTC_TRIG_CLR;
	g_rtc_wake       = 1;
}

uint8_t power_take_rtc_wake(void)
{
	uint8_t w = g_rtc_wake;

	g_rtc_wake = 0;
	return w;
}

void power_sleep_for_ms(uint32_t ms)
{
	if (ms) {
		/*
		 * Integer conversion rather than ch32fun's MS_TO_RTC(), which
		 * is a floating-point macro. 32768 ticks per second is
		 * ms * 4096 / 125 exactly; split so it stays in 32 bits for any
		 * ms up to the counter's daily wrap, with no soft-float and no
		 * 64-bit divide. RTC_setAlarm() handles that wrap itself.
		 */
		uint32_t ticks = (ms / 125u) * 4096u + ((ms % 125u) * 4096u) / 125u;

		RTC_setAlarm(ticks);
	}

	power_sleep();
}

void power_sleep(void)
{
	/*
	 * Mask SysTick first. This is the whole difference between sleeping
	 * and not.
	 *
	 * WFI returns on any *pending* interrupt, enabled or not at the core.
	 * SysTick fires at 1 kHz, so leaving it armed means the chip enters
	 * deep sleep, is woken within a millisecond, runs the loop, and sleeps
	 * again -- a thousand times a second. That costs more than simply
	 * idling, and it looks exactly like "sleep does nothing": the measured
	 * floor stays at the awake figure.
	 *
	 * Clearing the pending flags matters as much as masking: a SysTick
	 * that already fired would otherwise be waiting to return WFI
	 * immediately.
	 */
	SysTick->CTLR &= ~SYSTICK_CTLR_STIE;
	SysTick->SR    = 0;
	NVIC_ClearPendingIRQ(SysTick_IRQn);

	/*
	 * No RTC alarm is set, so a button edge is the only thing that can
	 * wake us. RB_SLP_GPIO_WAKE was armed in buttons_init().
	 */
	/*
	 * RB_PWR_MUST_0010 is documented in the register map as a field that
	 * must be written with 0010. ch32fun's helper does not include it;
	 * WCH's LowPower_Sleep() does, as (2 << 11). Passing it here means the
	 * helper ORs it into the plan it writes.
	 */
	LowPowerSleepWFI(RB_PWR_RAM2K | RB_PWR_RAMX | RB_PWR_EXTEND |
	                 RB_PWR_MUST_0010);

	/*
	 * Clear SLEEPDEEP. This is the fix for the LEDs.
	 *
	 * LowPowerSleepWFI() sets NVIC->SCTLR bit 2 before its WFI and does
	 * not clear it afterwards. Nothing about WFI clears it either. So from
	 * the first deep sleep onward, *every* WFI in the firmware is a deep
	 * sleep -- including the main loop's idle-until-next-tick, which is
	 * meant to be a plain halt with the clocks running.
	 *
	 * The result, visible in a capture, is the fob dropping to 0 uA for
	 * milliseconds at a time in the middle of an LED animation: each idle
	 * WFI partial-resets the chip and comes back on the next SysTick
	 * without running any of the restore below, because that path is a
	 * bare WFI. The WS2812 data pin flaps between frames and the picture
	 * lands shifted -- coherent, but on the wrong pixels.
	 *
	 * ch32fun's own LowPowerIdleWFI() clears this bit before its WFI, and
	 * the ble_beacon example happens to call it after every sleep, which
	 * is why the example never sees this.
	 */
	NVIC->SCTLR &= ~(1 << 2);

	/*
	 * Restore the timebase before anything else.
	 *
	 * This ordering is load-bearing. Everything below may delay, and
	 * ch32fun's Delay_Us() spins on SysTick->CNT with no timeout -- so if
	 * the counter is not running, it never returns. Waking is "a selective
	 * reset of the registers that are powered down in sleep mode" and
	 * SysTick is a core peripheral, so its control register cannot be
	 * assumed to have survived. Restoring it last, as an earlier version
	 * did, hangs the fob on its first wake.
	 */
	SysTick->CTLR = SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE | SYSTICK_CTLR_STCLK;
	SysTick->SR   = 0;
	SysTick->CMP  = SysTick->CNT + (FUNCONF_SYSTEM_CORE_CLOCK / 1000);

	/*
	 * Bring the DC-DC back *before* settling, not after.
	 *
	 * ch32fun's LowPowerSleepWFI() writes R16_POWER_PLAN from scratch and
	 * drops RB_PWR_DCDC_EN / RB_PWR_DCDC_PRE, so the core comes out of
	 * sleep on the LDO and DCDCEnable() switches it back -- a supply
	 * transition on every wake. (WCH's LowPower_Sleep() preserves those
	 * bits and never transitions.) That switch is the biggest disturbance
	 * in the whole wake path, so it goes first and the settling wait
	 * covers it, rather than the other way round.
	 */
	DCDCEnable();

	/*
	 * Now let the clock and supply settle before doing anything whose
	 * correctness depends on the clock rate.
	 *
	 * WCH's own LowPower_Sleep() ends with a DelayUs(20) after restoring
	 * R8_PLL_CONFIG; ch32fun's LowPowerSleepWFI() has no equivalent wait.
	 * Returning from it and immediately bit-banging WS2812 data or bringing
	 * the radio back means doing precisely-timed work on a clock that may
	 * not have relocked -- and at, say, 32 MHz instead of 60, a 17-cycle
	 * T0H becomes 531 ns against a 380 ns limit, so every 0 bit is misread.
	 *
	 * The wait is expressed in microseconds via SysTick, which is already
	 * running by this point. If the clock is still slow the delay is longer
	 * in real time rather than shorter, which is the safe direction.
	 */
	Delay_Us(FOB_WAKE_SETTLE_US);

	/*
	 * Then put back everything else the partial reset may have taken. The
	 * datasheet does not enumerate what survives, so this restores all of
	 * it rather than reasoning about each.
	 */
	power_apply_pin_config();  /* quiescing, FET gates, I2C idle state   */
	buttons_rearm();           /* pad modes and their edge interrupts    */
	ws2812_resume();           /* PB7 back to an output; no delay needed,
	                            * the line has been low all sleep        */
	led_anim_invalidate();     /* redraw rather than trust the shadow    */
	power_arm_gpio_wake();

	if (!FOB_DISABLE_RADIO)
		ble_adv_resume();     /* comes back in an undefined state       */
}
