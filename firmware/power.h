#ifndef LE_FOB_POWER_H
#define LE_FOB_POWER_H

#include <stdint.h>

/*
 * Power management: clock/regulator setup, quiescing pins nothing else owns,
 * deep sleep, and a timebase that survives it.
 */

/*
 * Call once, immediately after SystemInit() and before any other driver init
 * -- drivers that own a pin (buttons, ws2812) configure it afterwards and
 * are expected to override what this does.
 */
void power_init(void);

/*
 * Arm GPIO as a wake source. Called by power_init(); exposed so the host
 * tests can assert the full register state, because arming this partially
 * is a silent failure -- the fob sleeps and simply never comes back.
 */
void power_arm_gpio_wake(void);

/*
 * Re-apply the pin configuration power_init() established. Called on every
 * wake, because waking partially resets the chip and it is cheaper to put
 * everything back than to work out what survived.
 */
void power_apply_pin_config(void);

/*
 * Deep sleep until a button edge. Returns once awake, with the clocks and
 * the radio restored.
 *
 * Note that g_millis does NOT advance across this call: SysTick is clocked
 * from HCLK, which stops. Use power_rtc_*() for anything that must measure
 * real elapsed time.
 */
void power_sleep(void);

/*
 * Deep sleep for at most `ms` milliseconds, waking earlier on a button.
 * `ms` of 0 means no timed wake at all -- identical to power_sleep().
 * Returns once awake, with clocks, pins and radio restored.
 */
void power_sleep_for_ms(uint32_t ms);

/* Test-and-clear: did the RTC alarm fire since last asked? */
uint8_t power_take_rtc_wake(void);

/*
 * Idle until the next interrupt -- a plain WFI with the clocks running,
 * not a deep sleep. Use this for the "nothing to do until the next tick"
 * case; power_sleep() is for the "nothing to do at all" case.
 *
 * Exists because the two are one bit apart and that bit is sticky. See the
 * implementation.
 */
void power_idle(void);

/*
 * Free-running 32.768 kHz counter from the RTC domain, which keeps running
 * through deep sleep. Wraps once per day.
 */
uint32_t power_rtc_ticks(void);

/* Milliseconds between two power_rtc_ticks() readings, wrap included. */
uint32_t power_rtc_elapsed_ms(uint32_t from, uint32_t to);

#endif /* LE_FOB_POWER_H */
