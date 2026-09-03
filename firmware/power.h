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
 * Deep sleep until a button edge. Returns once awake, with the clocks and
 * the radio restored.
 *
 * Note that g_millis does NOT advance across this call: SysTick is clocked
 * from HCLK, which stops. Use power_rtc_*() for anything that must measure
 * real elapsed time.
 */
void power_sleep(void);

/*
 * Free-running 32.768 kHz counter from the RTC domain, which keeps running
 * through deep sleep. Wraps once per day.
 */
uint32_t power_rtc_ticks(void);

/* Milliseconds between two power_rtc_ticks() readings, wrap included. */
uint32_t power_rtc_elapsed_ms(uint32_t from, uint32_t to);

#endif /* LE_FOB_POWER_H */
