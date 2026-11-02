#include "ch32fun.h"
#include "board.h"
#include "charger.h"

#include "i2c.h"

/*
 * AW32001E registers used here. Reset states from the datasheet's register
 * map, quoted because two of them are the whole reason this file exists.
 */
#define AW_REG_POR_CTRL   0x01  /* reset 0xAC: CEB=1, charging DISABLED   */
#define AW_REG_CHG_CTRL   0x02  /* reset 0x0F: REG_RST, WD_RST, ICHG    */
#define AW_REG_RST        0x80  /* REG02H B7: 0->1 is a software reset  */
#define AW_REG_TIMER      0x05  /* reset 0x7A: EN_WD_DISCHG, WATCHDOG   */
#define AW_REG_MAIN_CTRL  0x06  /* reset 0xC0: EN_NTC, TMR2X_EN, FET_DIS*/
#define AW_REG_STATUS     0x08  /* reset 0x40, read-only                */
#define AW_REG_FAULT      0x09  /* reset 0x00, read-only                */
#define AW_REG_CHIP_ID    0x0A  /* reset 0x49                           */

#define AW_CHIP_ID 0x49

/* REG01H */
#define AW_CEB            0x08  /* B3: 0 = charge enable, 1 = disable */

/* REG02H */
#define AW_REG_RST_BIT    0x80  /* B7 */
#define AW_WD_TMR_RST     0x40  /* B6 */
#define AW_ICHG_MASK      0x3F  /* B5-B0: (code + 1) * 8 mA */
#define AW_ICHG_CODE      ((CHARGER_ICHG_MA / 8) - 1)

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

static int reg_read(uint8_t reg, uint8_t *val)
{
	return i2c_reg_read(CHARGER_I2C_ADDR, reg, val);
}

static int reg_write(uint8_t reg, uint8_t val)
{
	return i2c_reg_write(CHARGER_I2C_ADDR, reg, val);
}

/*
 * Read-modify-write, verified.
 *
 * Two things make a blind read-modify-write unsafe against this part on this
 * bus, and both have teeth:
 *
 *   A read can come back wrong. The vendored I2C library this driver first
 *   ran on did so routinely (see i2c.c); its replacement should not, but a
 *   corrupted read fed straight back into a write puts arbitrary bits into
 *   the register, so the check stays.
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
	if (!reg_read(reg, &a) || !reg_read(reg, &b) || a != b)
		return 0;

	uint8_t want = (uint8_t)((a & (uint8_t)~(clear | force_clear)) | set);

	if (want != a && !reg_write(reg, want))
		return 0;

	/* Confirm. If the part did not take it, the caller must not carry on
	 * as though it had. */
	if (!reg_read(reg, &b) || b != want)
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
	uint8_t r08, r09;

	out->valid = 0;
	if (!present)
		return 0;

	/* Two single-byte reads rather than one auto-incrementing pair: the
	 * I2C driver does single bytes only (see i2c.c). */
	if (!reg_read(AW_REG_STATUS, &r08) || !reg_read(AW_REG_FAULT, &r09))
		return 0;

	charger_decode(r08, r09, out);
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

/*
 * Apply every correction, in order, each one verified. Returns 1 if all of
 * them took. Safe to repeat: a register already holding the right value is
 * read but not written.
 */
static int configure(void)
{
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
	 * value" -- so the system rail drops, the MCU reboots, and the
	 * corrections below are silently undone.
	 *
	 * Disabling it is the only configuration consistent with sleeping, and
	 * it must happen before anything else, so no window exists where the
	 * timer is running against writes we have already made.
	 */
	if (!reg_update(AW_REG_TIMER, AW_WATCHDOG_MASK | AW_EN_WD_DISCHG, 0, 0))
		return 0;

	/*
	 * Correction 2: disable the NTC function.
	 *
	 * REG06H resets to 0xC0, so EN_NTC = 1, and with DIS_PCB_OTP = 0 that
	 * selects PCB_OTP (Table 5). On a reworked board the NTC divider's
	 * GND-side resistor is gone -- it drew ~213 uA continuously, most of
	 * the idle budget -- leaving NTC pulled to VDD, which is what the
	 * datasheet recommends for an unused NTC.
	 *
	 * Tied high, PCB_OTP does not trip, so the board runs. But EN_NTC = 1
	 * reads VDD as "maximally cold" and inhibits charging. Clearing EN_NTC
	 * disables NTC and PCB_OTP together and gives charging back. On an
	 * unmodified board this also stops the thermistor being consulted;
	 * the charger's own die-temperature regulation (TJ_REG) still applies.
	 *
	 * FET_DIS is force-cleared on this write rather than merely preserved.
	 * Preserving it would be correct if the value read back were
	 * trustworthy; forcing it clear is correct even when it is not.
	 */
	if (!reg_update(AW_REG_MAIN_CTRL, AW_EN_NTC, 0, AW_FET_DIS))
		return 0;

	/*
	 * Correction 3: size the charge current to the cell.
	 *
	 * REG02H resets to ICHG = 128 mA, about 1.3C on the 100 mAh cell.
	 * CHARGER_ICHG_MA in board.h sets it. Bits 7 and 6 are REG_RST and
	 * WD_TMR_RST: action bits, forced clear so this write can never be a
	 * software reset.
	 */
	if (!reg_update(AW_REG_CHG_CTRL, AW_ICHG_MASK,
	                (uint8_t)AW_ICHG_CODE, AW_REG_RST_BIT | AW_WD_TMR_RST))
		return 0;

	/*
	 * Correction 4: enable charging.
	 *
	 * REG01H resets to 0xAC, and bit 3 of that is CEB = 1: "charge
	 * disable (default)". Out of reset the AW32001 powers the system from
	 * USB and never charges the battery until a host clears CEB. Nothing
	 * did before this, which is why the status text only ever said "USB
	 * idle". Last, so charging starts with everything above already in
	 * place.
	 */
	return reg_update(AW_REG_POR_CTRL, AW_CEB, 0, 0);
}

int charger_init(void)
{
	uint8_t id, id2;

	present = 0;
	result  = CHARGER_NO_ACK;

	if (!i2c_open(CHARGER_I2C_KHZ))
		return 0;

	/*
	 * Probe with two agreeing reads, for the same reason the writes are
	 * verified: a single corrupted byte here would either reject a part
	 * that is present, or -- worse -- be a coincidental 0x49 on a bus with
	 * nothing on it. If two reads disagree, or agree on the wrong value,
	 * this is not our charger and nothing further is written to it.
	 */
	if (!reg_read(AW_REG_CHIP_ID, &id) || !reg_read(AW_REG_CHIP_ID, &id2))
		return 0;                       /* CHARGER_NO_ACK */

	if (id != id2 || id != AW_CHIP_ID) {
		result = CHARGER_BAD_ID;
		return 0;
	}

	present = 1;
	result  = CHARGER_OK;

	if (!configure()) {
		/*
		 * Something did not verify, and we are already in host mode, so
		 * doing nothing could leave a live watchdog we cannot kick. Put
		 * the part back to defaults and report the charger as absent;
		 * the fob runs fine without it, it just will not charge.
		 */
		reg_reset();
		present = 0;
		result  = CHARGER_CFG_FAILED;
		return 0;
	}

	return 1;
}

void charger_service(void)
{
	uint8_t r01, r05;

	if (!present) {
		/* Absent or unconfigured at boot. A retry is cheap, and the
		 * alternative is a fob that never charges until it is reset. */
		charger_init();
		return;
	}

	/*
	 * The part reverts every register to its defaults on a watchdog
	 * expiry or a software reset, and on a battery re-insert or a USB
	 * plug with no battery -- not all of which take the MCU down with it.
	 * CEB back at 1, or the watchdog running again, is the tell. A failed
	 * read is not: that is left for the next chirp to retry rather than
	 * rewriting a part we could not read.
	 */
	if (!reg_read(AW_REG_POR_CTRL, &r01) || !reg_read(AW_REG_TIMER, &r05))
		return;

	if ((r01 & AW_CEB) || (r05 & AW_WATCHDOG_MASK))
		charger_init();
}
