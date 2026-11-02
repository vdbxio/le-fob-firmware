/*
 * Host-side check of firmware/charger.c against a fake AW32001.
 *
 * The register corrections this driver makes are load-bearing and all of
 * them fail quietly if they regress: leave the watchdog running and the
 * charger reverts its own configuration and power-cycles the board every
 * 160 s while charging; leave EN_NTC or CEB set and the fob works perfectly
 * except that it never charges; leave ICHG alone and a 100 mAh cell charges
 * at 1.3C. None announces itself, so all are asserted here.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/charger.h"
#include "stub/i2c_fake.h"

uint8_t g_fake_rom[6];

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

/* Datasheet reset states. */
static void device_reset(void)
{
	memset(i2c_fake_reg, 0, sizeof(i2c_fake_reg));
	i2c_fake_reg[0x01] = 0xAC; /* CEB=1: charging disabled            */
	i2c_fake_reg[0x02] = 0x0F; /* ICHG = 128 mA                       */
	i2c_fake_reg[0x05] = 0x7A; /* EN_WD_DISCHG=0, WATCHDOG=11 (160 s) */
	i2c_fake_reg[0x06] = 0xC0; /* EN_NTC=1, TMR2X_EN=1, FET_DIS=0     */
	i2c_fake_reg[0x08] = 0x40;
	i2c_fake_reg[0x09] = 0x00;
	i2c_fake_reg[0x0A] = 0x49; /* chip ID */
	i2c_fake_log_len   = 0;
	i2c_fake_reads     = 0;
	i2c_fake_present   = 1;
	i2c_fake_corrupt_reads = 0;
	i2c_fake_clean_first   = 0;
}

/* Index of the last log entry touching `reg`, or -1. */
static int last_write_to(uint8_t reg)
{
	for (int i = i2c_fake_log_len - 1; i >= 0; i--)
		if (i2c_fake_log_reg[i] == reg)
			return i;
	return -1;
}

/* Index of the first log entry touching `reg`, or -1. */
static int first_write_to(uint8_t reg)
{
	for (int i = 0; i < i2c_fake_log_len; i++)
		if (i2c_fake_log_reg[i] == reg)
			return i;
	return -1;
}

int main(void)
{
	printf("\n== probe ==\n");

	device_reset();
	check(charger_init() == 1, "finds the part when the chip ID matches");
	check(charger_result() == CHARGER_OK, "and reports OK");
	check(charger_present(), "reports present afterwards");
	check(i2c_fake_open_khz == CHARGER_I2C_KHZ, "bus opened at the configured rate");

	device_reset();
	i2c_fake_reg[0x0A] = 0x12; /* wrong ID */
	check(charger_init() == 0, "rejects a part with the wrong chip ID");
	check(charger_result() == CHARGER_BAD_ID, "and says so: BAD_ID");
	check(!charger_present(), "and does not claim to be present");
	check(i2c_fake_log_len == 0, "writes nothing to an unidentified device");

	device_reset();
	i2c_fake_present = 0;
	check(charger_init() == 0, "survives a bus with nothing on it");
	check(charger_result() == CHARGER_NO_ACK, "and says so: NO_ACK");

	printf("\n== the watchdog must end up off ==\n");

	device_reset();
	charger_init();

	check((i2c_fake_reg[0x05] & 0x60) == 0,
	      "WATCHDOG bits cleared (was 11 = 160 s at reset)");
	check((i2c_fake_reg[0x05] & 0x80) == 0, "EN_WD_DISCHG left clear");

	/*
	 * Ordering: any write moves the part into host mode, where the
	 * watchdog is live. Disabling it has to come first, or there is a
	 * window in which a timer we cannot service is running against
	 * configuration we have already applied.
	 */
	int wd = first_write_to(0x05), ntc = first_write_to(0x06);

	check(wd >= 0, "the timer register is written at all");
	check(wd >= 0 && ntc >= 0 && wd < ntc,
	      "the watchdog is disabled before anything else is touched");

	printf("\n== NTC must end up disabled ==\n");

	check((i2c_fake_reg[0x06] & 0x80) == 0,
	      "EN_NTC cleared, so PCB_OTP and NTC are both off");
	check((i2c_fake_reg[0x06] & 0x40) != 0,
	      "TMR2X_EN preserved, not clobbered by a blind write");

	/*
	 * FET_DIS puts the part into shipping mode, from which this board
	 * cannot recover without USB. Nothing here should ever set it.
	 */
	check((i2c_fake_reg[0x06] & 0x20) == 0,
	      "FET_DIS still clear -- never put the fob into shipping mode");

	printf("\n== charge current sized to the cell ==\n");

	check((i2c_fake_reg[0x02] & 0x3F) == CHARGER_ICHG_MA / 8 - 1,
	      "ICHG set from CHARGER_ICHG_MA (was 128 mA at reset)");
	check((i2c_fake_reg[0x02] & 0xC0) == 0,
	      "REG_RST and WD_TMR_RST not set by the ICHG write");

	printf("\n== charging must end up enabled ==\n");

	check((i2c_fake_reg[0x01] & 0x08) == 0,
	      "CEB cleared -- the part does not charge until this happens");
	check(i2c_fake_reg[0x01] == (0xAC & ~0x08),
	      "the rest of REG01H left at its reset value");

	int ceb = last_write_to(0x01), ichg = last_write_to(0x02);

	check(ceb >= 0 && ceb == i2c_fake_log_len - 1,
	      "charging enabled last, after everything else is in place");
	check(ichg >= 0 && ichg < ceb && ntc < ceb,
	      "current and NTC configured before charging starts");

	printf("\n== idempotent ==\n");

	int before = i2c_fake_log_len;

	charger_init();
	check(i2c_fake_log_len == before,
	      "a second init writes nothing, the values already being right");

	printf("\n== a corrupt read must not become a corrupt write ==\n");

	/*
	 * The I2C library can hand back a wrong byte (it re-reads DATAR when
	 * BTF is set). Fed into a blind read-modify-write that reaches REG06,
	 * a flipped bit 5 is FET_DIS -- shipping mode, and on this board the
	 * only exit is USB. The driver must not turn a bad read into a write.
	 */
	/* Corruption during the probe: the part is simply not identified, and
	 * nothing is written to a device we have not recognised. */
	device_reset();
	i2c_fake_corrupt_reads = 1;
	check(charger_init() == 0, "a corrupt probe read means the part is not claimed");
	check(i2c_fake_log_len <= 2, "and nothing beyond register pointers is written");

	/* Corruption after a clean probe, i.e. during configuration. This is
	 * the dangerous case: a read-modify-write of REG06 with a flipped
	 * bit 5 would set FET_DIS and strand the board in shipping mode. */
	device_reset();
	i2c_fake_clean_first   = 2;   /* let the probe succeed */
	i2c_fake_corrupt_reads = 8;
	int r = charger_init();
	check(r == 0, "init fails rather than proceeding on unverifiable reads");
	check(!charger_present(), "and does not report the charger as present");
	check(charger_result() == CHARGER_CFG_FAILED, "and says so: CFG_FAILED");
	check((i2c_fake_reg[0x06] & 0x20) == 0,
	      "FET_DIS still clear -- a corrupt read never becomes a corrupt write");

	/*
	 * Reading alone puts the part in host mode, where an unkicked watchdog
	 * eventually drops the rail. Bailing out must not leave it there.
	 */
	int didreset = 0;
	for (int i = 0; i < i2c_fake_log_len; i++)
		if (i2c_fake_log_reg[i] == 0x02 && (i2c_fake_log_val[i] & 0x80))
			didreset = 1;
	check(didreset, "software reset issued, returning the part to default mode");

	printf("\n== service: repair a part that reverted ==\n");

	device_reset();
	charger_init();
	before = i2c_fake_log_len;
	i2c_fake_reads = 0;
	charger_service();
	check(i2c_fake_log_len == before, "a healthy part is not written to");
	check(i2c_fake_reads <= 2, "and costs no more than two reads");

	/* Watchdog expiry or a battery re-insert: every register back to its
	 * reset value while the MCU kept running. */
	device_reset();
	charger_init();
	device_reset();
	charger_service();
	check((i2c_fake_reg[0x01] & 0x08) == 0, "CEB cleared again after a revert");
	check((i2c_fake_reg[0x05] & 0x60) == 0, "watchdog off again");
	check((i2c_fake_reg[0x06] & 0x80) == 0, "EN_NTC off again");
	check((i2c_fake_reg[0x02] & 0x3F) == CHARGER_ICHG_MA / 8 - 1, "ICHG set again");

	/* Not found at boot: the service retries from scratch. */
	device_reset();
	i2c_fake_present = 0;
	charger_init();
	i2c_fake_present = 1;
	charger_service();
	check(charger_present() && charger_result() == CHARGER_OK,
	      "a part missing at boot is picked up on a later service");
	check((i2c_fake_reg[0x01] & 0x08) == 0, "and configured to charge");

	/* A read that fails during service is not grounds for rewriting. */
	device_reset();
	charger_init();
	before = i2c_fake_log_len;
	i2c_fake_present = 0;
	charger_service();
	i2c_fake_present = 1;
	check(i2c_fake_log_len == before && charger_present(),
	      "a failed service read changes nothing");

	printf("\n== status decode ==\n");

	charger_status_t s;

	charger_decode(0x00, 0x00, &s);
	check(s.state == CHARGER_NOT_CHARGING, "CHG_STAT 00 -> idle");
	charger_decode(0x08, 0x00, &s);
	check(s.state == CHARGER_PRECHARGE, "CHG_STAT 01 -> pre-charge");
	charger_decode(0x10, 0x00, &s);
	check(s.state == CHARGER_CHARGING, "CHG_STAT 10 -> charging");
	charger_decode(0x18, 0x00, &s);
	check(s.state == CHARGER_DONE, "CHG_STAT 11 -> done");

	charger_decode(0x02, 0x00, &s);
	check(s.power_good, "PG_STAT set -> VBUS good");
	charger_decode(0x00, 0x00, &s);
	check(!s.power_good, "PG_STAT clear -> power fail");

	charger_decode(0x80, 0x00, &s);
	check(s.watchdog_fault, "watchdog fault surfaced");
	charger_decode(0x01, 0x00, &s);
	check(s.in_thermal_regulation, "thermal regulation surfaced");

	charger_decode(0x00, 0x20, &s);
	check(s.faults == CHARGER_FAULT_VIN, "VIN fault");
	charger_decode(0x00, 0x03, &s);
	check(s.faults == (CHARGER_FAULT_NTC_HOT | CHARGER_FAULT_NTC_COLD),
	      "both NTC fault bits");

	/* REG09H's top two bits are EN_SHIPPING_DGL, configuration rather than
	 * fault, and must not leak into the fault set. */
	charger_decode(0x00, 0xC0, &s);
	check(s.faults == 0, "EN_SHIPPING_DGL is not reported as a fault");

	printf("\n== read path ==\n");

	device_reset();
	charger_init();
	i2c_fake_reg[0x08] = 0x10 | 0x02; /* charging, power good */
	i2c_fake_reg[0x09] = 0x00;
	check(charger_read(&s) == 1, "status read succeeds");
	check(s.valid && s.state == CHARGER_CHARGING && s.power_good,
	      "and decodes REG08H and REG09H together");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
