#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

/*
 * ch32fun build configuration for Le'Fob (WCH CH592X, QFN-32).
 */

#define FUNCONF_USE_HSI           0  /* CH5xx has no HSI */
#define FUNCONF_USE_HSE           1
#define CLK_SOURCE_CH5XX          CLK_SOURCE_PLL_60MHz
#define FUNCONF_SYSTEM_CORE_CLOCK 60000000

/*
 * Run SysTick straight off HCLK (60 MHz) instead of HCLK/8.
 *
 * This is load-bearing, not a preference: ws2812.c times its bit-bang
 * against the SysTick counter, and a 60 MHz tick gives 16.7 ns of
 * resolution against WS2812 bit fields that are only ~350 ns wide.
 * At HCLK/8 (7.5 MHz, 133 ns/tick) the quantisation error alone would
 * be a quarter of a bit. Changing this back will break the LEDs.
 */
#define FUNCONF_SYSTICK_USE_HCLK  1

#define FUNCONF_DEBUG_HARDFAULT   0
#define FUNCONF_USE_CLK_SEC       0

/* printf() over the SWIO debug interface (minichlink -T to view).
 * Note: this is silent on a board that has had its debug module
 * disabled for USB-ISP flashing (minichlink -n). See README. */
#define FUNCONF_USE_DEBUGPRINTF   1

#endif
