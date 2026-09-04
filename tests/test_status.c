/*
 * Host-side check of firmware/status.c, the line the chirp carries.
 *
 * Every string must fit BTHOME_STATUS_TEXT_MAX or it is silently cut in
 * the advertisement, so the length of every possible output is asserted,
 * along with the precedence -- a fault must win over a happy state.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/bthome.h"
#include "../firmware/charger.h"
#include "../firmware/status.h"

uint8_t g_fake_rom[6];

/* status_text() is not under test; these satisfy the link only. */
int charger_present(void) { return 0; }
int charger_read(charger_status_t *s) { (void)s; return 0; }
charger_result_t charger_result(void) { return CHARGER_NO_ACK; }

static int failures;
static void check(int c, const char *w)
{
	printf("  %-52s %s\n", w, c ? "ok" : "FAIL");
	if (!c) failures++;
}

static void expect(const char *what, int present, charger_status_t *s, const char *want)
{
	char buf[32];

	status_format(buf, sizeof(buf), present, s);
	int ok = strcmp(buf, want) == 0;
	int fits = strlen(buf) <= BTHOME_STATUS_TEXT_MAX;

	printf("  %-30s -> %-12s %s%s\n", what, buf,
	       ok ? "ok" : "FAIL", fits ? "" : "  (TOO LONG)");
	if (!ok || !fits) failures++;
}

int main(void)
{
	charger_status_t s;

	printf("\n== without a charger: say why ==\n");
	{
		char b[32];
		status_format_absent(b, sizeof b, 0, CHARGER_NO_ACK);
		check(!strcmp(b, "chgr off") && strlen(b) <= BTHOME_STATUS_TEXT_MAX,
		      "driver compiled out -> 'chgr off'");
		status_format_absent(b, sizeof b, 1, CHARGER_NO_ACK);
		check(!strcmp(b, "chgr nak") && strlen(b) <= BTHOME_STATUS_TEXT_MAX,
		      "driver on, no ACK -> 'chgr nak' (bus / pull-ups)");
		status_format_absent(b, sizeof b, 1, CHARGER_BAD_ID);
		check(!strcmp(b, "chgr id?") && strlen(b) <= BTHOME_STATUS_TEXT_MAX,
		      "driver on, wrong chip id -> 'chgr id?'");
		status_format_absent(b, sizeof b, 1, CHARGER_CFG_FAILED);
		check(!strcmp(b, "chgr cfg!") && strlen(b) <= BTHOME_STATUS_TEXT_MAX,
		      "driver on, config did not verify -> 'chgr cfg!'");
	}
	memset(&s, 0, sizeof s);
	s.valid = 0;
	expect("present but status read failed", 1, &s, "chgr read!");

	printf("\n== power source and charge state ==\n");
	memset(&s, 0, sizeof s); s.valid = 1;
	expect("VBUS absent", 1, &s, "battery");
	s.power_good = 1;
	s.state = CHARGER_NOT_CHARGING; expect("VBUS, idle",       1, &s, "USB idle");
	s.state = CHARGER_PRECHARGE;    expect("VBUS, pre-charge", 1, &s, "USB pre");
	s.state = CHARGER_CHARGING;     expect("VBUS, charging",   1, &s, "USB chg");
	s.state = CHARGER_DONE;         expect("VBUS, done",       1, &s, "USB done");

	printf("\n== a fault outranks a happy state ==\n");
	s.state = CHARGER_CHARGING; s.power_good = 1;
	s.faults = CHARGER_FAULT_THERMAL; expect("thermal, while charging", 1, &s, "fault therm");
	s.faults = CHARGER_FAULT_BAT;     expect("battery OVP",            1, &s, "fault bat");
	s.faults = CHARGER_FAULT_VIN;     expect("input fault",            1, &s, "fault vin");
	s.faults = CHARGER_FAULT_TIMER;   expect("safety timer",           1, &s, "fault timer");
	s.faults = CHARGER_FAULT_NTC_HOT; expect("NTC hot",                1, &s, "fault ntc");
	s.faults = CHARGER_FAULT_NTC_COLD;expect("NTC cold",               1, &s, "fault ntc");
	s.faults = 0; s.watchdog_fault = 1; expect("watchdog expired",     1, &s, "fault wdog");

	printf("\n== buffer discipline ==\n");
	char tiny[4];
	memset(&s, 0, sizeof s); s.valid = 1; s.power_good = 1; s.state = CHARGER_CHARGING;
	status_format(tiny, sizeof tiny, 1, &s);
	check(strlen(tiny) == 3 && tiny[3] == 0, "never writes past a small buffer");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
