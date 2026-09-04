/*
 * Host-side check of firmware/chirp.c, the status-chirp scheduler.
 *
 * It is pure arithmetic over the RTC tick counter, so the interesting
 * cases are the boundaries: exactly on time, one tick early, and across
 * the counter's daily wrap, which is not a power of two and so cannot be
 * handled by plain modular subtraction.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/chirp.h"
#include "../firmware/power.h"

uint8_t g_fake_rom[6];

/* The real one lives in power.c; only the pure arithmetic is needed here. */
uint32_t power_rtc_elapsed_ms(uint32_t from, uint32_t to)
{
	uint32_t ticks = (to >= from) ? (to - from) : (to + RTC_MAX_COUNT - from);
	return (uint32_t)(((uint64_t)ticks * 1000u) / 32768u);
}

static int failures;
static void check(int c, const char *w)
{
	printf("  %-56s %s\n", w, c ? "ok" : "FAIL");
	if (!c) failures++;
}

#define MS(ms) ((uint32_t)(((uint64_t)(ms) * 32768u) / 1000u))

int main(void)
{
	const uint32_t I = CHIRP_INTERVAL_MS;

	printf("\n== schedule (interval %lu ms) ==\n", (unsigned long)I);

	chirp_init(1000);
	check(!chirp_due(1000), "not due at the instant of init");
	check(chirp_ms_until(1000) == I, "a full interval remains at init");

	check(!chirp_due(1000 + MS(I) - 40), "one tick short: not due");
	check(chirp_due(1000 + MS(I) + 40), "one tick past: due");
	check(chirp_ms_until(1000 + MS(I) + 40) == 0, "and nothing remains");

	check(chirp_ms_until(1000 + MS(I / 2)) == I - I / 2,
	      "half way through: half an interval remains");

	printf("\n== marking sent reschedules ==\n");

	uint32_t t = 1000 + MS(I) + 40;
	chirp_mark_sent(t);
	check(!chirp_due(t), "not due immediately after sending");
	check(chirp_due(t + MS(I) + 40), "due again one interval later");

	printf("\n== across the daily counter wrap ==\n");

	/* Last chirp shortly before the wrap; now shortly after it. */
	uint32_t before = RTC_MAX_COUNT - MS(I / 2);
	chirp_init(before);
	uint32_t after = MS(I / 2) + 40;     /* wrapped past zero */
	check(chirp_due(after), "due when the interval spans the wrap");
	check(chirp_ms_until(before + MS(I / 4)) == I - I / 4,
	      "remaining time correct before the wrap");

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
