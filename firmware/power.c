#include "ch32fun.h"
#include "board.h"
#include "power.h"
#include "ble_adv.h"
#include "led_anim.h"
#include "buttons.h"

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
	power_arm_gpio_wake();
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
	/*
	 * RB_PWR_MUST_0010 is documented in the register map as a field that
	 * must be written with 0010. ch32fun's helper does not include it;
	 * WCH's LowPower_Sleep() does, as (2 << 11). Passing it here means the
	 * helper ORs it into the plan it writes.
	 */
	LowPowerSleepWFI(RB_PWR_RAM2K | RB_PWR_RAMX | RB_PWR_EXTEND |
	                 RB_PWR_MUST_0010);

	/*
	 * Waking from sleep generates a partial reset -- the datasheet is
	 * explicit that it is "a selective reset of the registers that are
	 * powered down in sleep mode". Anything configured before sleeping has
	 * to be assumed gone, so the wake source and the button interrupts it
	 * listens to are re-armed on every wake rather than only at boot.
	 *
	 * Getting this wrong looks like a fob that wakes once or twice and
	 * then never again.
	 */
	power_arm_gpio_wake();
	buttons_rearm();

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
