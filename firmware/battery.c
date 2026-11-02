#include "ch32fun.h"
#include "board.h"
#include "battery.h"

#define ADC_CH_VBAT 14            /* internal: the chip's own supply */
#define ADC_SAMPLES 16
#define ADC_SPIN    2000u         /* a conversion is a few us */

/*
 * -12 dB PGA, which is what WCH's own ADC_InterBATSampInit() uses: the
 * only gain whose range reaches 3.3 V. Datasheet table 15-2 gives
 * Vin = Vref * (raw / 512 - 3), Vref = 1.05 V.
 */
static uint16_t raw_to_mv(uint32_t raw)
{
	int32_t mv = (int32_t)(1050u * raw / 512u) - 3150;

	return mv < 0 ? 0 : (uint16_t)mv;
}

uint16_t battery_read_mv(void)
{
	uint32_t sum = 0;

	R8_TKEY_CFG    &= ~RB_TKEY_PWR_ON;
	R8_ADC_CHANNEL  = ADC_CH_VBAT;
	R8_ADC_CFG      = RB_ADC_POWER_ON | RB_ADC_BUF_EN; /* -12 dB, 3.2 MHz */
	R8_ADC_CONVERT &= ~RB_ADC_PGA_GAIN2;

	/* One extra conversion up front, thrown away, while the freshly
	 * powered ADC and buffer settle. */
	for (int i = 0; i <= ADC_SAMPLES; i++) {
		uint32_t n = ADC_SPIN;

		R8_ADC_CONVERT |= RB_ADC_START;
		while ((R8_ADC_CONVERT & RB_ADC_START) && --n)
			;
		if (!n) {
			R8_ADC_CFG = 0;
			return 0;
		}
		if (i)
			sum += R16_ADC_DATA & RB_ADC_DATA;
	}

	R8_ADC_CFG = 0; /* power the ADC back down */

	return raw_to_mv((sum + ADC_SAMPLES / 2) / ADC_SAMPLES);
}

int battery_low_update(battery_state_t *st, uint16_t mv, int on_usb)
{
	if (!mv)
		return st->low; /* no reading: nothing learned */

	if (mv > st->ref_mv)
		st->ref_mv = mv;

	if (on_usb) {
		st->low = 0;
		return 0;
	}

	if (mv + BATT_LOW_DROP_MV <= st->ref_mv || mv <= BATT_LOW_FLOOR_MV)
		st->low = 1;
	else if (mv + BATT_LOW_CLEAR_MV >= st->ref_mv && mv > BATT_LOW_FLOOR_MV)
		st->low = 0;

	return st->low;
}
