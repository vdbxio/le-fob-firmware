#ifndef LE_FOB_I2C_H
#define LE_FOB_I2C_H

#include <stdint.h>

/*
 * Polled master on the CH592's hardware I2C, single-byte register access
 * only -- which is all the AW32001 needs, and the only transfer shape whose
 * sequencing on this peripheral is simple enough to get right by
 * construction. See i2c.c for why the vendored library was replaced.
 *
 * Every call re-opens the peripheral if it is not in the state i2c_open()
 * left it in, so callers need not care whether a deep sleep happened in
 * between.
 */

/* Configure the peripheral at `khz` (<= 100). Returns 1 on success. */
int i2c_open(uint16_t khz);

/* Returns 1 on success, 0 on NACK, bus error or timeout. */
int i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *val);
int i2c_reg_write(uint8_t addr, uint8_t reg, uint8_t val);

#endif /* LE_FOB_I2C_H */
