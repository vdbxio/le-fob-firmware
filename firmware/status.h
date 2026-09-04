#ifndef LE_FOB_STATUS_H
#define LE_FOB_STATUS_H

#include <stddef.h>
#include "charger.h"

/*
 * The short human-readable line the status chirp carries. At most
 * BTHOME_STATUS_TEXT_MAX (11) characters, because that is what fits.
 *
 * Reads the charger; if the driver is off or the part did not answer, says
 * so rather than guessing at the power source.
 */
const char *status_text(void);

/*
 * The formatting alone, over a given charger reading, so it can be
 * exercised on the host. `present` is whether the charger answered at all;
 * `s` is ignored when it did not.
 */
void status_format(char *buf, size_t n, int present, const charger_status_t *s);

#endif /* LE_FOB_STATUS_H */
