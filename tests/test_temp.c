/*
 * Host-side check of firmware/temp.c: the calibration arithmetic and the
 * give-up paths. The ADC itself is stubbed; the point is that a known
 * reading against a known calibration word comes out as the temperature
 * WCH's own adc_to_temperature_celsius() would give, in hundredths.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/temp.h"

unsigned char  R8_TKEY_CFG, R8_ADC_CHANNEL, R8_ADC_CFG, R8_TEM_SENSOR;
unsigned char  adc_convert_reg;
int            adc_stub_completes;
unsigned short R16_ADC_DATA;
volatile uint32_t g_fake_tmp25c;

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

/* Every conversion completes and returns `adc`. */
static int16_t read_with(uint16_t adc, uint32_t cal)
{
	adc_stub_completes = 1;
	R16_ADC_DATA       = adc;
	g_fake_tmp25c      = cal;
	return temp_read_c100();
}

int main(void)
{
	printf("\n== calibration arithmetic ==\n");

	check(read_with(1500, 1500) == 2500,
	      "reading equal to the 25 C point reads 25.00 C");
	check(read_with(1500 + 57, 1500) == 2500 + 57 * 10000 / 283,
	      "57 counts above it is ~20 C warmer (283 counts per 100 C)");
	check(read_with(1500 - 142, 1500) == 2500 - 5017,
	      "below it goes negative, in hundredths");
	check(read_with(1500, (30u << 16) | 1500) == 3000,
	      "a non-zero calibration temperature is honoured");

	printf("\n== give-up paths ==\n");

	check(read_with(0, 1500) == TEMP_INVALID,
	      "an impossible result (-28 C) is reported as invalid");
	check(R8_ADC_CFG == 0 && R8_TEM_SENSOR == 0,
	      "ADC and sensor powered down afterwards");

	adc_stub_completes = 0; /* START sticks: conversion never finishes */
	g_fake_tmp25c      = 1500;
	check(temp_read_c100() == TEMP_INVALID,
	      "a conversion that never ends is invalid");
	check(R8_ADC_CFG == 0 && R8_TEM_SENSOR == 0,
	      "and still powers both down");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
