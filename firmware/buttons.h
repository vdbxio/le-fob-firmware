#ifndef LE_FOB_BUTTONS_H
#define LE_FOB_BUTTONS_H

#include <stdint.h>
#include "board.h"

/*
 * Interrupt-woken, tick-decoded button layer.
 *
 * Division of labour:
 *   - A falling-edge GPIO interrupt on each of the four pads exists purely
 *     to notice that something happened and to be a wake source once deep
 *     sleep is switched on. It does no decoding.
 *   - buttons_tick(), called every millisecond from the main loop, does the
 *     debouncing and the gesture state machine.
 *
 * Keeping the decode out of the ISR means the interrupt stays a few
 * instructions long, and means the gesture timers are driven by one clock
 * rather than by however many times a bouncing contact happened to fire.
 */

/* Configure the four pads and arm their interrupts. Call once at boot. */
void buttons_init(void);

/*
 * Advance debounce and gesture state. `now` is a free-running millisecond
 * counter. Safe to call with gaps (it works off absolute timestamps, not
 * call counts), so a skipped tick during a BLE burst costs nothing.
 */
void buttons_tick(uint32_t now);

/*
 * Pop the oldest decoded gesture.
 *
 * Returns 1 and fills *button (0..NUM_BUTTONS-1) and *event (a BTHOME_EVT_*
 * code) if one was waiting, otherwise 0.
 */
int buttons_pop_event(uint8_t *button, uint8_t *event);

/* Nonzero while any pad is physically down, or while a gesture is still
 * being decoded (a tap waiting to see if it becomes a double, a press
 * waiting to become a hold). The sleep decision keys off this. */
int buttons_busy(void);

/*
 * Test and clear the "a GPIO interrupt fired" flag set by the ISR. Used to
 * confirm what woke the chip.
 */
uint8_t buttons_take_wake(void);

/* Human-readable name for a BTHOME_EVT_* code, for logging. */
const char *buttons_event_name(uint8_t event);

#endif /* LE_FOB_BUTTONS_H */
