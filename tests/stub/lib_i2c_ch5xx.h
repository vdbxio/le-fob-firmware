/*
 * Host stand-in for ch32fun's CH5xx I2C library, backed by a fake 16-register
 * device so charger.c can be run and its register traffic asserted on.
 *
 * Models what the AW32001 datasheet describes: a write of one byte sets the
 * address pointer, a write of two bytes is address+data, and reads walk
 * forward from the pointer (the part auto-increments on ACK).
 *
 * Every write is also recorded in order, because the *sequence* matters --
 * the watchdog has to be disabled before anything else is written.
 */
#ifndef LEFOB_TEST_I2C_H
#define LEFOB_TEST_I2C_H

#include <stdint.h>

#define I2C_FAKE_NREG 16
#define I2C_FAKE_NLOG 32

extern uint8_t i2c_fake_reg[I2C_FAKE_NREG];
extern uint8_t i2c_fake_present;         /* 0 = nothing ACKs */
extern uint8_t i2c_fake_log_reg[I2C_FAKE_NLOG];
extern uint8_t i2c_fake_log_val[I2C_FAKE_NLOG];
extern int     i2c_fake_log_len;
extern int     i2c_fake_init_khz;

static uint8_t i2c_fake_ptr;

static inline uint8_t i2c_init(uint16_t khz)
{
	i2c_fake_init_khz = khz;
	return 0;
}

static inline uint8_t i2c_writeData(uint8_t addr, uint8_t *data, uint8_t len)
{
	(void)addr;
	if (!i2c_fake_present)
		return 3; /* ACK failure */

	if (len == 1) {
		i2c_fake_ptr = data[0];
	} else if (len >= 2) {
		i2c_fake_ptr = data[0];
		for (uint8_t i = 1; i < len; i++) {
			uint8_t r = (uint8_t)(data[0] + i - 1);

			if (r < I2C_FAKE_NREG)
				i2c_fake_reg[r] = data[i];
			if (i2c_fake_log_len < I2C_FAKE_NLOG) {
				i2c_fake_log_reg[i2c_fake_log_len] = r;
				i2c_fake_log_val[i2c_fake_log_len] = data[i];
				i2c_fake_log_len++;
			}
		}
	}
	return 0;
}

static inline uint8_t i2c_readData(uint8_t addr, uint8_t *data, uint8_t len)
{
	(void)addr;
	if (!i2c_fake_present)
		return 3;

	for (uint8_t i = 0; i < len; i++) {
		uint8_t r = (uint8_t)(i2c_fake_ptr + i);

		data[i] = (r < I2C_FAKE_NREG) ? i2c_fake_reg[r] : 0xFF;
	}
	return 0;
}

#endif
