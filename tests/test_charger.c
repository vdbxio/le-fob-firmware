/*
 * Host-side check of firmware/charger.c against a fake AW32001.
 *
 * The two register corrections this driver makes are load-bearing and both
 * fail quietly if they regress: leave the watchdog running and the charger
 * reverts its own configuration and power-cycles the board every 160 s while
 * charging; leave EN_NTC set and the fob works perfectly except that it never
 * charges. Neither announces itself, so both are asserted here.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/charger.h"
#include "stub/lib_i2c_ch5xx.h"

uint8_t g_fake_rom[6];

/* Backing store for the fake device. */
uint8_t i2c_fake_reg[I2C_FAKE_NREG];
uint8_t i2c_fake_present;
uint8_t i2c_fake_log_reg[I2C_FAKE_NLOG];
uint8_t i2c_fake_log_val[I2C_FAKE_NLOG];
int     i2c_fake_log_len;
int     i2c_fake_init_khz;

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
	i2c_fake_reg[0x01] = 0xAC;
	i2c_fake_reg[0x05] = 0x7A; /* EN_WD_DISCHG=0, WATCHDOG=11 (160 s) */
	i2c_fake_reg[0x06] = 0xC0; /* EN_NTC=1, TMR2X_EN=1, FET_DIS=0     */
	i2c_fake_reg[0x08] = 0x40;
	i2c_fake_reg[0x09] = 0x00;
	i2c_fake_reg[0x0A] = 0x49; /* chip ID */
	i2c_fake_log_len   = 0;
	i2c_fake_present   = 1;
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
	check(charger_present(), "reports present afterwards");
	check(i2c_fake_init_khz == CHARGER_I2C_KHZ, "bus opened at the configured rate");

	device_reset();
	i2c_fake_reg[0x0A] = 0x12; /* wrong ID */
	check(charger_init() == 0, "rejects a part with the wrong chip ID");
	check(!charger_present(), "and does not claim to be present");
	check(i2c_fake_log_len == 0, "writes nothing to an unidentified device");

	device_reset();
	i2c_fake_present = 0;
	check(charger_init() == 0, "survives a bus with nothing on it");

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

	printf("\n== idempotent ==\n");

	int before = i2c_fake_log_len;

	charger_init();
	check(i2c_fake_log_len == before,
	      "a second init writes nothing, the values already being right");

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
	      "and decodes both registers from one transaction");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
