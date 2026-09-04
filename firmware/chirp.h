#ifndef LE_FOB_CHIRP_H
#define LE_FOB_CHIRP_H

#include <stdint.h>

/*
 * Scheduling for the periodic status chirp.
 *
 * Pure bookkeeping over the RTC tick counter, which is the only clock that
 * keeps running through deep sleep. No hardware here; the RTC alarm that
 * actually wakes the chip is armed by power_sleep_for_ms() using the number
 * this module hands back. Kept separate so it can be tested on the host.
 */

/* Start the schedule: the first chirp is one interval from `rtc_now`. */
void chirp_init(uint32_t rtc_now);

/* Nonzero once a full CHIRP_INTERVAL_MS has elapsed since the last chirp. */
int chirp_due(uint32_t rtc_now);

/* Milliseconds until the next chirp is due; 0 if it already is. */
uint32_t chirp_ms_until(uint32_t rtc_now);

/* Record that a chirp went out now, scheduling the next one. */
void chirp_mark_sent(uint32_t rtc_now);

#endif /* LE_FOB_CHIRP_H */
