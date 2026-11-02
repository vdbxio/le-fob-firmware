/*
 * Host stand-in for firmware/i2c.c, backed by a fake 16-register device so
 * charger.c can be run and its register traffic asserted on. Linked in
 * place of the real driver; the declarations charger.c sees are the real
 * ones in firmware/i2c.h.
 *
 * Every write is recorded in order, because the *sequence* matters -- the
 * watchdog has to be disabled before anything else is written, and
 * charging enabled only after everything else.
 */
#ifndef LEFOB_TEST_I2C_FAKE_H
#define LEFOB_TEST_I2C_FAKE_H

#include <stdint.h>

#define I2C_FAKE_NREG 16
#define I2C_FAKE_NLOG 64

extern uint8_t i2c_fake_reg[I2C_FAKE_NREG];
extern uint8_t i2c_fake_present;         /* 0 = nothing ACKs */
extern uint8_t i2c_fake_log_reg[I2C_FAKE_NLOG];
extern uint8_t i2c_fake_log_val[I2C_FAKE_NLOG];
extern int     i2c_fake_log_len;
extern int     i2c_fake_open_khz;
extern int     i2c_fake_reads;           /* reads performed, for cost checks */

/* When nonzero, the next N reads return corrupted data. */
extern int     i2c_fake_corrupt_reads;
extern int     i2c_fake_clean_first;  /* let this many reads through untouched */

#endif
