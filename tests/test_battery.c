/*
 * Host-side check of the battery-low decision in firmware/battery.c.
 *
 * The flag is the only battery warning the fob can give, so both ways it
 * can go wrong matter: never firing (a fob that dies without warning) and
 * flickering on noise around the threshold (an alert that gets muted).
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/battery.h"

unsigned char  R8_TKEY_CFG, R8_ADC_CHANNEL, R8_ADC_CFG;
unsigned char  adc_convert_reg;
int            adc_stub_completes;
unsigned short R16_ADC_DATA;

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

int main(void)
{
	battery_state_t b = { 0 };

	printf("\n== in regulation ==\n");

	check(!battery_low_update(&b, 3310, 0), "a regulated rail is not low");
	check(b.ref_mv == 3310, "and becomes the reference");
	check(!battery_low_update(&b, 3290, 0), "20 mV of noise is not low");

	printf("\n== falling out of regulation ==\n");

	check(battery_low_update(&b, 3310 - BATT_LOW_DROP_MV, 0),
	      "BATT_LOW_DROP_MV under the reference is low");
	check(battery_low_update(&b, 3310 - BATT_LOW_DROP_MV + 15, 0),
	      "and stays low on a small recovery (hysteresis)");
	check(!battery_low_update(&b, 3310 - BATT_LOW_CLEAR_MV, 0),
	      "clears back within BATT_LOW_CLEAR_MV");

	printf("\n== USB ==\n");

	battery_low_update(&b, 3200, 0);
	check(b.low, "low before plugging in");
	check(!battery_low_update(&b, 3200, 1), "USB clears it");

	printf("\n== no reference ==\n");

	battery_state_t cold = { 0 };

	check(battery_low_update(&cold, BATT_LOW_FLOOR_MV, 0),
	      "booted on a flat cell: the absolute floor still fires");
	check(battery_low_update(&cold, 0, 0) == 1,
	      "a failed reading (0 mV) leaves the flag alone");
	check(cold.ref_mv == BATT_LOW_FLOOR_MV, "and does not move the reference");

	printf("\n== conversion ==\n");

	/* Table 15-2, -12 dB: Vin = 1.05 V * (raw / 512 - 3). */
	adc_stub_completes = 1;
	R16_ADC_DATA       = 3145;
	check(battery_read_mv() == 3299, "raw 3145 at -12 dB reads 3.299 V");
	check(R8_ADC_CFG == 0, "and the ADC is powered back down");

	/* A conversion that never finishes: give up rather than hang. */
	adc_stub_completes = 0;
	check(battery_read_mv() == 0, "a conversion that never ends reads as 0 mV");
	check(R8_ADC_CFG == 0, "and the ADC is powered down then too");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
