#include "board.h"
#include "chirp.h"
#include "power.h"

static uint32_t last_sent;

void chirp_init(uint32_t rtc_now)
{
	last_sent = rtc_now;
}

int chirp_due(uint32_t rtc_now)
{
	return power_rtc_elapsed_ms(last_sent, rtc_now) >= CHIRP_INTERVAL_MS;
}

uint32_t chirp_ms_until(uint32_t rtc_now)
{
	uint32_t elapsed = power_rtc_elapsed_ms(last_sent, rtc_now);

	return elapsed >= CHIRP_INTERVAL_MS ? 0 : CHIRP_INTERVAL_MS - elapsed;
}

void chirp_mark_sent(uint32_t rtc_now)
{
	last_sent = rtc_now;
}
