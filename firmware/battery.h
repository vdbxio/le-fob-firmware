#ifndef LE_FOB_BATTERY_H
#define LE_FOB_BATTERY_H

#include <stdint.h>

/*
 * Battery-low detection without a battery voltage sense.
 *
 * The board has no divider on the cell. What the CH592 can measure is its
 * own supply (ADC channel 14), which is the 3V3 LDO output: flat while the
 * cell can hold the LDO in regulation, then following the cell down,
 * roughly VBAT minus a few tens of mV. So the reading says nothing until a
 * LiPo is nearly flat -- around 3.35 V, the last few percent -- and then it
 * says so reliably. That is "charge me", not a fuel gauge.
 */

/* Supply voltage in mV, averaged. 0 if the ADC did not answer. */
uint16_t battery_read_mv(void);

typedef struct {
	uint16_t ref_mv;   /* highest reading seen: the regulated level */
	uint8_t  low;
} battery_state_t;

/*
 * Feed one reading; returns the low flag.
 *
 * Judged against the highest reading seen since boot rather than a fixed
 * number, because the ADC reference is only good to +/-1.4% (+/-45 mV at
 * 3.3 V) and the LDO has its own tolerance: comparing the chip against
 * itself cancels both. A fixed floor still catches a fob booted on an
 * already-flat cell, where the reference is never seen. USB clears the
 * flag -- a fob on the charger is not one to warn about.
 */
int battery_low_update(battery_state_t *st, uint16_t mv, int on_usb);

#endif /* LE_FOB_BATTERY_H */
