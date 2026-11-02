#ifndef LE_FOB_STATUS_H
#define LE_FOB_STATUS_H

#include <stddef.h>
#include "bthome.h"
#include "charger.h"

/*
 * Everything the status chirp carries: supply voltage, battery low,
 * charging, and a short human-readable line -- at most
 * BTHOME_STATUS_TEXT_MAX (9) characters, because that is what fits.
 *
 * Reads the charger and the supply. If the charger driver is off or the
 * part did not answer, the text says so rather than guessing at the power
 * source, and "charging" reads as no. The text pointer stays valid until
 * the next call.
 */
void status_collect(bthome_status_t *out);

/* The battery-low flag as of the last status_collect(). */
int status_battery_low(void);

/*
 * The formatting alone, over a given charger reading, so it can be
 * exercised on the host. `present` is whether the charger answered at all;
 * `s` is ignored when it did not.
 */
void status_format(char *buf, size_t n, int present, const charger_status_t *s);

/* As above, but for the not-present case: says *why*. */
void status_format_absent(char *buf, size_t n, int driver_enabled,
                          charger_result_t why);

#endif /* LE_FOB_STATUS_H */
