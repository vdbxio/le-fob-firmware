#ifndef LE_FOB_WS2812_H
#define LE_FOB_WS2812_H

#include <stdint.h>

/*
 * Bit-banged WS2812 driver for the PIXEL0 / "LED0" chain (4 pixels on PB7).
 *
 * See ws2812.c for why this is bit-banged rather than driven by ch32fun's
 * SPI+DMA WS2812 helper.
 */

/* Configure PIN_WS2812 as a push-pull output and park the line low. */
void ws2812_init(void);

/*
 * The pin half of ws2812_init(), without the latch delay.
 *
 * For use on the wake path, where two things make the delay both unwanted
 * and unsafe: the data line has been held low for the whole sleep, which is
 * orders of magnitude longer than the 280 us the chain needs to latch; and
 * the delay spins on the SysTick counter, which may not be running yet.
 */
void ws2812_resume(void);

/*
 * Push `count` pixels out of the chain. `grb` is 3 bytes per pixel in
 * wire order: green, red, blue.
 *
 * Blocks for ~30 us per pixel with interrupts disabled (see ws2812.c).
 * Callers should not invoke this faster than the ~300 us chain reset needs.
 */
void ws2812_show(const uint8_t *grb, int count);

#endif /* LE_FOB_WS2812_H */
