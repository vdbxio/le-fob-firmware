#include "../../firmware/i2c.h"
#include "i2c_fake.h"

uint8_t i2c_fake_reg[I2C_FAKE_NREG];
uint8_t i2c_fake_present;
uint8_t i2c_fake_log_reg[I2C_FAKE_NLOG];
uint8_t i2c_fake_log_val[I2C_FAKE_NLOG];
int     i2c_fake_log_len;
int     i2c_fake_open_khz;
int     i2c_fake_reads;
int     i2c_fake_corrupt_reads;
int     i2c_fake_clean_first;

int i2c_open(uint16_t khz)
{
	i2c_fake_open_khz = khz;
	return 1;
}

int i2c_reg_write(uint8_t addr, uint8_t reg, uint8_t val)
{
	(void)addr;
	if (!i2c_fake_present)
		return 0;

	if (reg < I2C_FAKE_NREG)
		i2c_fake_reg[reg] = val;
	if (i2c_fake_log_len < I2C_FAKE_NLOG) {
		i2c_fake_log_reg[i2c_fake_log_len] = reg;
		i2c_fake_log_val[i2c_fake_log_len] = val;
		i2c_fake_log_len++;
	}
	return 1;
}

int i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *val)
{
	(void)addr;
	if (!i2c_fake_present)
		return 0;

	i2c_fake_reads++;
	*val = (reg < I2C_FAKE_NREG) ? i2c_fake_reg[reg] : 0xFF;

	if (i2c_fake_clean_first > 0)
		i2c_fake_clean_first--;
	else if (i2c_fake_corrupt_reads > 0) {
		i2c_fake_corrupt_reads--;
		*val ^= 0x20;   /* flip FET_DIS, the dangerous bit */
	}
	return 1;
}
