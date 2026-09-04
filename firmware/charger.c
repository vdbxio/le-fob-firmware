#include "ch32fun.h"
#include "board.h"
#include "charger.h"

#include "lib_i2c_ch5xx.h"

/*
 * AW32001E registers used here. Reset states from the datasheet's register
 * map, quoted because two of them are the whole reason this file exists.
 */
#define AW_REG_POR_CTRL   0x01  /* CEB, EN_HIZ                          */
#define AW_REG_CHG_CTRL   0x02  /* WD_RST (B6), REG_RST (B7)            */
#define AW_REG_TIMER      0x05  /* reset 0x7A: EN_WD_DISCHG, WATCHDOG   */
#define AW_REG_MAIN_CTRL  0x06  /* reset 0xC0: EN_NTC, TMR2X_EN, FET_DIS*/
#define AW_REG_STATUS     0x08  /* reset 0x40, read-only                */
#define AW_REG_FAULT      0x09  /* reset 0x00, read-only                */
#define AW_REG_CHIP_ID    0x0A  /* reset 0x49                           */

#define AW_CHIP_ID 0x49

/* REG05H */
#define AW_WATCHDOG_MASK  0x60  /* B6-B5: 00 off, 01 40s, 10 80s, 11 160s */
#define AW_EN_WD_DISCHG   0x80  /* B7 */

/* REG06H */
#define AW_EN_NTC         0x80  /* B7 */
#define AW_FET_DIS        0x20  /* B5 */

/* REG08H */
#define AW_WATCHDOG_FAULT 0x80  /* B7 */
#define AW_CHG_STAT_MASK  0x18  /* B4-B3 */
#define AW_CHG_STAT_SHIFT 3
#define AW_PG_STAT        0x02  /* B1: 0 = power fail, 1 = power good */
#define AW_THERM_STAT     0x01  /* B0 */

static uint8_t present;

/* ------------------------------------------------------------------ */
/* Register access                                                     */
/* ------------------------------------------------------------------ */

/*
 * The datasheet's read cycle is: write the register address, then "STOP
 * condition followed with START condition or REPEAT START", then read. The
 * plain stop-then-start form is explicitly allowed, which is what the two
 * calls below produce. Sequential reads auto-increment the address, so a
 * multi-byte read walks consecutive registers.
 */
static int reg_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
	if (i2c_writeData(CHARGER_I2C_ADDR, &reg, 1) != 0)
		return 0;
	return i2c_readData(CHARGER_I2C_ADDR, buf, len) == 0;
}

static int reg_write(uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = { reg, val };

	return i2c_writeData(CHARGER_I2C_ADDR, buf, 2) == 0;
}

/* Read, apply a mask, write back only if it actually changed. */
static int reg_update(uint8_t reg, uint8_t clear, uint8_t set)
{
	uint8_t v;

	if (!reg_read(reg, &v, 1))
		return 0;

	uint8_t n = (uint8_t)((v & (uint8_t)~clear) | set);

	if (n == v)
		return 1;
	return reg_write(reg, n);
}

/* ------------------------------------------------------------------ */
/* Status                                                              */
/* ------------------------------------------------------------------ */

void charger_decode(uint8_t reg08, uint8_t reg09, charger_status_t *out)
{
	out->valid                 = 1;
	out->state                 = (charger_chg_state_t)
	                             ((reg08 & AW_CHG_STAT_MASK) >> AW_CHG_STAT_SHIFT);
	out->power_good            = (reg08 & AW_PG_STAT) ? 1 : 0;
	out->watchdog_fault        = (reg08 & AW_WATCHDOG_FAULT) ? 1 : 0;
	out->in_thermal_regulation = (reg08 & AW_THERM_STAT) ? 1 : 0;

	/* REG09H's low six bits are the fault flags; the top two are
	 * EN_SHIPPING_DGL, which is configuration rather than status. */
	out->faults = (uint8_t)(reg09 & 0x3F);
}

int charger_read(charger_status_t *out)
{
	uint8_t buf[2];

	out->valid = 0;
	if (!present)
		return 0;

	/* REG08 and REG09 are adjacent, so one transaction covers both. */
	if (!reg_read(AW_REG_STATUS, buf, 2))
		return 0;

	charger_decode(buf[0], buf[1], out);
	return 1;
}

const char *charger_state_name(charger_chg_state_t state)
{
	switch (state) {
	case CHARGER_NOT_CHARGING: return "idle";
	case CHARGER_PRECHARGE:    return "pre-charge";
	case CHARGER_CHARGING:     return "charging";
	case CHARGER_DONE:         return "done";
	default:                   return "?";
	}
}

int charger_present(void) { return present; }

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

int charger_init(void)
{
	uint8_t id;

	present = 0;

	/*
	 * The peripheral drives these open-drain once enabled; they only need
	 * to start as inputs with pull-up. power_init() already leaves them
	 * that way, but say so here too so this file does not depend on the
	 * order it is called in.
	 */
	funPinMode(PIN_I2C_SCL, GPIO_CFGLR_IN_PUPD);
	funPinMode(PIN_I2C_SDA, GPIO_CFGLR_IN_PUPD);

	if (i2c_init(CHARGER_I2C_KHZ) != 0)
		return 0;

	if (!reg_read(AW_REG_CHIP_ID, &id, 1) || id != AW_CHIP_ID)
		return 0;

	present = 1;

	/*
	 * Correction 1: disable the watchdog.
	 *
	 * REG05H resets to 0x7A, which is WATCHDOG = 11 = 160 s. It is
	 * inactive in discharge mode (EN_WD_DISCHG = 0) but live while
	 * charging -- and writing any register at all moves the part from
	 * "default mode" into "host mode", where the host is expected to kick
	 * it via REG02H[6].
	 *
	 * We cannot kick it. The fob sleeps for minutes at a time, and waking
	 * every 160 s purely to service a charger would cost more than the
	 * charger does. Left running it would expire, and on expiry "both the
	 * Q1 and Q2 are turned off, and most registers return to the default
	 * value" -- so the system rail drops, the MCU reboots, and correction
	 * 2 below is silently undone.
	 *
	 * Disabling it is the only configuration consistent with sleeping, and
	 * it must happen before anything else, so no window exists where the
	 * timer is running against writes we have already made.
	 */
	reg_update(AW_REG_TIMER, AW_WATCHDOG_MASK | AW_EN_WD_DISCHG, 0);

	/*
	 * Correction 2: disable the NTC function.
	 *
	 * REG06H resets to 0xC0, so EN_NTC = 1, and with DIS_PCB_OTP = 0 that
	 * selects PCB_OTP (Table 5). This board has no thermistor -- the NTC
	 * divider was removed because it drew ~213 uA continuously, which was
	 * most of the idle budget -- and NTC is tied to VDD instead, which is
	 * what the datasheet recommends for an unused NTC.
	 *
	 * Tied high, PCB_OTP does not trip, so the board runs. But EN_NTC = 1
	 * reads VDD as "maximally cold" and inhibits charging. Clearing EN_NTC
	 * disables NTC and PCB_OTP together and gives charging back.
	 *
	 * The failure this avoids is quiet: the fob runs, sleeps, advertises,
	 * and simply never charges.
	 */
	reg_update(AW_REG_MAIN_CTRL, AW_EN_NTC, 0);

	return 1;
}
