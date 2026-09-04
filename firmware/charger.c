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
#define AW_REG_RST        0x80  /* REG02H B7: 0->1 is a software reset  */
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
static charger_result_t result = CHARGER_NO_ACK;

charger_result_t charger_result(void) { return result; }

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

/*
 * Read-modify-write, verified.
 *
 * Two things make a blind read-modify-write unsafe against this part on this
 * bus, and both have teeth:
 *
 *   The I2C library can silently corrupt a received byte -- when BTF is set
 *   it re-reads DATAR and overwrites the value it just took. A corrupted
 *   read fed straight back into a write puts arbitrary bits into the
 *   register.
 *
 *   REG06H bit 5 is FET_DIS. Setting it puts the AW32001 into shipping mode,
 *   where all paths are disabled and the board draws nothing -- and on this
 *   board the only exit is plugging in USB, because INT reaches nothing that
 *   is powered. One flipped bit in a register write is the difference
 *   between a working fob and a dead one.
 *
 * So: read twice and require agreement before trusting the value, never let
 * `set` bits outside the mask through, and read back after writing to
 * confirm the part took what was intended.
 */
static int reg_update(uint8_t reg, uint8_t clear, uint8_t set, uint8_t force_clear)
{
	uint8_t a, b;

	/* A corrupted read is not repeatable, so two matching reads is a cheap
	 * filter against the BTF bug above. */
	if (!reg_read(reg, &a, 1) || !reg_read(reg, &b, 1) || a != b)
		return 0;

	uint8_t want = (uint8_t)((a & (uint8_t)~(clear | force_clear)) | set);

	if (want != a && !reg_write(reg, want))
		return 0;

	/* Confirm. If the part did not take it, the caller must not carry on
	 * as though it had. */
	if (!reg_read(reg, &b, 1) || b != want)
		return 0;

	return 1;
}

/*
 * Return the part to its power-on defaults and to "default mode", where it
 * no longer expects a watchdog kick.
 *
 * This is the recovery path. Merely reading a register writes the address
 * pointer, which is enough to move the AW32001 into host mode -- and in host
 * mode an unkicked watchdog eventually turns Q1 and Q2 off, dropping the
 * system rail. So if configuration cannot be completed and verified, leaving
 * the part as-is is not neutral; it is a fob that power-cycles itself. A
 * software reset is the way back.
 */
static void reg_reset(void)
{
	reg_write(AW_REG_CHG_CTRL, AW_REG_RST);
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

	result = CHARGER_NO_ACK;

	if (i2c_init(CHARGER_I2C_KHZ) != 0)
		return 0;

	/*
	 * Probe with two agreeing reads, for the same reason the writes are
	 * verified: a single corrupted byte here would either reject a part
	 * that is present, or -- worse -- be a coincidental 0x49 on a bus with
	 * nothing on it. If two reads disagree, or agree on the wrong value,
	 * this is not our charger and nothing further is written to it.
	 */
	uint8_t id2;

	if (!reg_read(AW_REG_CHIP_ID, &id, 1) ||
	    !reg_read(AW_REG_CHIP_ID, &id2, 1))
		return 0;                       /* CHARGER_NO_ACK */

	if (id != id2 || id != AW_CHIP_ID) {
		result = CHARGER_BAD_ID;
		return 0;
	}

	present = 1;
	result  = CHARGER_OK;

	int ok = 1;

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
	ok = reg_update(AW_REG_TIMER, AW_WATCHDOG_MASK | AW_EN_WD_DISCHG, 0, 0);

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
	/*
	 * FET_DIS is force-cleared on this write rather than merely preserved.
	 * Preserving it would be correct if the value read back were
	 * trustworthy; forcing it clear is correct even when it is not.
	 */
	if (ok)
		ok = reg_update(AW_REG_MAIN_CTRL, AW_EN_NTC, 0, AW_FET_DIS);

	if (!ok) {
		/*
		 * Something did not verify. We are already in host mode -- the
		 * register pointer write that starts any read is enough for
		 * that -- so doing nothing would leave a live watchdog we
		 * cannot kick. Put the part back to defaults and report the
		 * charger as absent; the fob runs fine without it.
		 */
		reg_reset();
		present = 0;
		result  = CHARGER_CFG_FAILED;
		return 0;
	}

	return 1;
}
