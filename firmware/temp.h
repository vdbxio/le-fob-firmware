#ifndef LE_FOB_TEMP_H
#define LE_FOB_TEMP_H

#include <stdint.h>

/*
 * The CH592's on-die temperature sensor, for the status chirp.
 *
 * It measures the silicon, not the air: in a pocket it reads body-warm, on
 * the charger it climbs a few degrees above ambient. WCH quote no accuracy
 * figure and the factory calibration is a single point at 25 C, so treat
 * it as +/- a few degrees. Good enough to notice a fob that is hot.
 */

#define TEMP_INVALID INT16_MIN

/* Die temperature in hundredths of a degree C, or TEMP_INVALID. */
int16_t temp_read_c100(void);

#endif /* LE_FOB_TEMP_H */
