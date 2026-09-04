#include "ch32fun.h"
#include "board.h"
#include "power.h"
#include "ble_adv.h"
#include "led_anim.h"

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
static void quiesce_unused_pins(void)
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

	/* Castellations and unconnected analog pins: pull up so they are not
	 * left floating. */
	funPinMode(PA4,  GPIO_CFGLR_IN_PUPD);   /* charger nIRQ, unused here */
	funPinMode(PA5,  GPIO_CFGLR_IN_PUPD);
	funPinMode(PA8,  GPIO_CFGLR_IN_PUPD);
	funPinMode(PA12, GPIO_CFGLR_IN_PUPD);   /* DISP_RST, OLED is DNP */
	funPinMode(PA13, GPIO_CFGLR_IN_PUPD);
	funPinMode(PA15, GPIO_CFGLR_IN_PUPD);
	funPinMode(PB12, GPIO_CFGLR_IN_PUPD);   /* UART1 RX */
	funPinMode(PB13, GPIO_CFGLR_IN_PUPD);   /* UART1 TX */

	/* I2C already has board pull-ups; match them rather than float. */
	funPinMode(PB14, GPIO_CFGLR_IN_PUPD);
	funPinMode(PB15, GPIO_CFGLR_IN_PUPD);
}

void power_init(void)
{
	/*
	 * The internal DC-DC cuts active current to roughly 60% of running off
	 * the LDO (datasheet 5.1). Enable it before anything starts drawing.
	 */
	DCDCEnable();

	/*
	 * Run the 32 kHz domain from the internal LSI rather than the LSE
	 * crystal. X2 is fitted, so LSE is available and would be more
	 * accurate -- worth revisiting when the chirp interval wants to be
	 * precise -- but LSI starts faster and needs no crystal settling.
	 */
	LSIEnable();

	RTC_init();   /* zero the counters, enable the trigger */
	SleepInit();  /* arm RTC wake and its interrupt */

	quiesce_unused_pins();
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
void RTC_IRQHandler(void) __attribute__((interrupt));
void RTC_IRQHandler(void)
{
	R8_RTC_FLAG_CTRL = RB_RTC_TRIG_CLR;
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
	LowPowerSleepWFI(RB_PWR_RAM2K | RB_PWR_RAMX | RB_PWR_EXTEND);

	/* The DC-DC is switched off across sleep and the radio comes back in
	 * an undefined state; both have to be re-established before use. */
	DCDCEnable();

	if (!FOB_DISABLE_RADIO)
		ble_adv_resume();

	/*
	 * Restart the millisecond tick. g_millis did not advance while we were
	 * out -- SysTick is clocked from HCLK, which stopped -- so everything
	 * timed against it resumes where it left off rather than jumping.
	 */
	SysTick->SR    = 0;
	SysTick->CMP   = SysTick->CNT + (FUNCONF_SYSTEM_CORE_CLOCK / 1000);
	SysTick->CTLR |= SYSTICK_CTLR_STIE;
}
