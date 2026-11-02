#include "ch32fun.h"
#include "board.h"
#include "i2c.h"

/*
 * Why not ch32fun's lib_i2c_ch5xx.h, which this replaces:
 *
 *   Single-byte reads cleared ADDR with ACK still enabled and armed STOP
 *   only after the byte arrived. That works when nothing interrupts the few
 *   instructions between them; when something does (SysTick, the radio),
 *   the byte is ACKed, the slave starts sending the next one, and the STOP
 *   cannot be generated because the slave is holding SDA. The bus stays
 *   BUSY and every transaction after it times out.
 *
 *   Multi-byte reads re-read DATAR whenever BTF was set, overwriting the
 *   byte just taken with the next one.
 *
 *   Nothing waited for a STOP to go out before the next START was asked
 *   for, and nothing recovered a bus left BUSY.
 *
 *   It also printf'd from i2c_init().
 *
 * All of which fits "chgr cfg!": a verify read that fails part-way through
 * configuration. This driver does single-byte transfers only, in the order
 * the STM32F1-family reference manual prescribes for this peripheral (the
 * CH59x I2C is that block), with the NACK/STOP arming done with interrupts
 * masked.
 */

#define SYS_MHZ (FUNCONF_SYSTEM_CORE_CLOCK / 1000000)

/*
 * Spin budget for any one flag. A byte at 100 kHz is 90 us; this is a few
 * milliseconds at 60 MHz, so it only ever expires on a bus that is not
 * going to answer.
 */
#define I2C_SPIN 30000u

#define I2C_ERRORS (RB_I2C_AF | RB_I2C_BERR | RB_I2C_ARLO | RB_I2C_OVR)

static uint16_t open_khz;
static uint16_t open_ccr;
static uint8_t  need_reset;

static uint16_t ccr_for(uint16_t khz)
{
	uint16_t ccr = (uint16_t)((SYS_MHZ * 1000u) / (2u * khz));

	return ccr < 4 ? 4 : ccr;
}

static void hw_setup(uint16_t khz)
{
	/* The peripheral drives these open-drain once enabled; they only need
	 * to start as inputs with pull-up. power.c leaves them that way too. */
	funPinMode(PIN_I2C_SCL, GPIO_CFGLR_IN_PU);
	funPinMode(PIN_I2C_SDA, GPIO_CFGLR_IN_PU);

	R16_I2C_CTRL1 = RB_I2C_SWRST;
	R16_I2C_CTRL1 = 0;

	R16_I2C_CTRL2  = SYS_MHZ & RB_I2C_FREQ;
	R16_I2C_RTR    = SYS_MHZ + 1;        /* standard mode: 1000 ns rise */
	open_ccr       = ccr_for(khz);
	R16_I2C_CKCFGR = open_ccr;           /* standard mode, F_S = 0      */

	R16_I2C_CTRL1 = RB_I2C_PE;
	open_khz      = khz;
	need_reset    = 0;
}

/*
 * A slave left mid-byte (by a reset, or a transfer we abandoned) can hold
 * SDA low indefinitely, and the peripheral will then never see the bus go
 * idle. Clock it out by hand: up to nine SCL pulses until SDA is released,
 * then a STOP. Open-drain emulated -- low is an output driving 0, high is
 * letting go into the pull-up -- so nothing ever fights a slave.
 */
static void bus_recover(void)
{
	R16_I2C_CTRL1 = 0;

	funDigitalWrite(PIN_I2C_SCL, FUN_LOW);
	funDigitalWrite(PIN_I2C_SDA, FUN_LOW);
	funPinMode(PIN_I2C_SDA, GPIO_CFGLR_IN_PU);
	funPinMode(PIN_I2C_SCL, GPIO_CFGLR_IN_PU);
	Delay_Us(10);

	for (int i = 0; i < 9 && !funDigitalRead(PIN_I2C_SDA); i++) {
		funPinMode(PIN_I2C_SCL, GPIO_CFGLR_OUT_2Mhz_PP);
		Delay_Us(10);
		funPinMode(PIN_I2C_SCL, GPIO_CFGLR_IN_PU);
		Delay_Us(10);
	}

	/* STOP: SDA rises while SCL is high. */
	funPinMode(PIN_I2C_SDA, GPIO_CFGLR_OUT_2Mhz_PP);
	Delay_Us(10);
	funPinMode(PIN_I2C_SCL, GPIO_CFGLR_IN_PU);
	Delay_Us(10);
	funPinMode(PIN_I2C_SDA, GPIO_CFGLR_IN_PU);
	Delay_Us(10);
}

int i2c_open(uint16_t khz)
{
	if (khz == 0 || khz > 100)
		return 0;

	hw_setup(khz);

	if (R16_I2C_STAR2 & RB_I2C_BUSY) {
		bus_recover();
		hw_setup(khz);
	}
	return !(R16_I2C_STAR2 & RB_I2C_BUSY);
}

/*
 * Before every transaction: is the peripheral still configured as we left
 * it? Across a deep sleep it may not be -- the datasheet does not say which
 * peripheral registers survive -- and after an abandoned transfer it is
 * reset deliberately.
 */
static int bus_ready(void)
{
	if (!open_khz)
		return 0;

	if (need_reset || !(R16_I2C_CTRL1 & RB_I2C_PE) ||
	    R16_I2C_CKCFGR != open_ccr)
		return i2c_open(open_khz);

	if (R16_I2C_STAR2 & RB_I2C_BUSY)
		return i2c_open(open_khz);

	return 1;
}

static int wait_flag(uint16_t flag)
{
	for (uint32_t n = I2C_SPIN; n; n--) {
		uint16_t s = R16_I2C_STAR1;

		if (s & flag)
			return 1;
		if (s & I2C_ERRORS)
			return 0;
	}
	return 0;
}

/* STOP is cleared by hardware once the condition has gone out. Starting
 * the next transfer before then is how the bus gets confused. */
static void wait_stop_sent(void)
{
	for (uint32_t n = I2C_SPIN; n && (R16_I2C_CTRL1 & RB_I2C_STOP); n--)
		;
}

static int fail(void)
{
	R16_I2C_CTRL1 |= RB_I2C_STOP;
	wait_stop_sent();
	R16_I2C_STAR1 = (uint16_t)~I2C_ERRORS;  /* error flags are write-0-to-clear */
	need_reset = 1;
	return 0;
}

/* START, address, and the ADDR flag cleared. */
static int address(uint8_t addr, uint8_t rd)
{
	R16_I2C_CTRL1 |= RB_I2C_START;
	if (!wait_flag(RB_I2C_SB))
		return 0;
	R16_I2C_DATAR = (uint16_t)((addr << 1) | rd);  /* STAR1 read + this clears SB */
	return wait_flag(RB_I2C_ADDR);
}

int i2c_reg_write(uint8_t addr, uint8_t reg, uint8_t val)
{
	if (!bus_ready())
		return 0;

	if (!address(addr, 0))
		return fail();
	(void)R16_I2C_STAR2;                 /* STAR1 then STAR2 clears ADDR */

	R16_I2C_DATAR = reg;
	if (!wait_flag(RB_I2C_TxE))
		return fail();
	R16_I2C_DATAR = val;
	if (!wait_flag(RB_I2C_BTF))          /* both bytes out and ACKed */
		return fail();

	R16_I2C_CTRL1 |= RB_I2C_STOP;
	wait_stop_sent();
	return 1;
}

int i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *val)
{
	if (!bus_ready())
		return 0;

	/* Register pointer, then a repeated START into the read. The AW32001
	 * accepts either a repeated START or STOP+START here. */
	if (!address(addr, 0))
		return fail();
	(void)R16_I2C_STAR2;

	R16_I2C_DATAR = reg;
	if (!wait_flag(RB_I2C_BTF))
		return fail();

	if (!address(addr, 1))
		return fail();

	/*
	 * One byte: NACK must be selected before ADDR is cleared, because
	 * clearing ADDR is what starts the byte, and STOP must be armed before
	 * that byte ends. Masked, so an interrupt cannot land between them and
	 * let the byte be ACKed.
	 */
	R16_I2C_CTRL1 &= ~RB_I2C_ACK;
	__disable_irq();
	(void)R16_I2C_STAR1;
	(void)R16_I2C_STAR2;                 /* ADDR cleared: reception starts */
	R16_I2C_CTRL1 |= RB_I2C_STOP;
	__enable_irq();

	if (!wait_flag(RB_I2C_RxNE))
		return fail();
	*val = (uint8_t)R16_I2C_DATAR;

	wait_stop_sent();
	return 1;
}
