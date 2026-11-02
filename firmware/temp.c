#include "ch32fun.h"
#include "board.h"
#include "temp.h"

#define ADC_CH_VTEMP 15
#define ADC_SAMPLES  16
#define ADC_SPIN     2000u

int16_t temp_read_c100(void)
{
	uint32_t sum = 0;

	/*
	 * WCH's ADC_InterTSSampInit(): sensor powered, differential mode,
	 * +6 dB PGA. The sensor needs a moment after power-up, which the
	 * discarded first conversion below covers.
	 */
	R8_TKEY_CFG    &= ~RB_TKEY_PWR_ON;
	R8_TEM_SENSOR   = RB_TEM_SEN_PWR_ON;
	R8_ADC_CHANNEL  = ADC_CH_VTEMP;
	R8_ADC_CFG      = RB_ADC_POWER_ON | RB_ADC_DIFF_EN | (3u << 4);
	R8_ADC_CONVERT &= ~RB_ADC_PGA_GAIN2;

	for (int i = 0; i <= ADC_SAMPLES; i++) {
		uint32_t n = ADC_SPIN;

		R8_ADC_CONVERT |= RB_ADC_START;
		while ((R8_ADC_CONVERT & RB_ADC_START) && --n)
			;
		if (!n) {
			R8_ADC_CFG    = 0;
			R8_TEM_SENSOR = 0;
			return TEMP_INVALID;
		}
		if (i)
			sum += R16_ADC_DATA & RB_ADC_DATA;
	}

	R8_ADC_CFG    = 0;
	R8_TEM_SENSOR = 0;

	/*
	 * Factory calibration word: low half is the ADC reading at the
	 * calibration temperature, high half is that temperature in whole
	 * degrees (0 means 25). The slope, 283 counts per 100 C, is WCH's
	 * adc_to_temperature_celsius(); it is the same for every part.
	 */
	uint32_t cal  = *LEFOB_ROM_TMP25C_ADDR;
	int32_t  c25  = (int32_t)(cal & 0xFFFF);
	int32_t  t25  = (int32_t)((cal >> 16) & 0xFFFF);
	int32_t  adc  = (int32_t)((sum + ADC_SAMPLES / 2) / ADC_SAMPLES);
	int32_t  c100 = (t25 ? t25 : 25) * 100 + (adc - c25) * 10000 / 283;

	if (c100 < -4000 || c100 > 12500)
		return TEMP_INVALID; /* not a temperature this fob can be at */

	return (int16_t)c100;
}
