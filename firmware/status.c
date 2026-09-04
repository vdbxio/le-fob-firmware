#include <string.h>
#include "board.h"
#include "bthome.h"
#include "status.h"

static void put(char *buf, size_t n, const char *s)
{
	size_t i = 0;

	while (s[i] && i + 1 < n) {
		buf[i] = s[i];
		i++;
	}
	buf[i] = 0;
}

void status_format(char *buf, size_t n, int present, const charger_status_t *s)
{
	if (!present || !s->valid) {
		/* Either the driver is compiled out or the part is not on the
		 * bus. Both are "we cannot see the charger", and that is more
		 * useful to say than a guess about the power source. */
		put(buf, n, "no chgr");
		return;
	}

	/* A fault outranks everything: it is the thing to go and look at. */
	if (s->faults & CHARGER_FAULT_THERMAL)  { put(buf, n, "fault therm"); return; }
	if (s->faults & CHARGER_FAULT_BAT)      { put(buf, n, "fault bat");   return; }
	if (s->faults & CHARGER_FAULT_VIN)      { put(buf, n, "fault vin");   return; }
	if (s->faults & CHARGER_FAULT_TIMER)    { put(buf, n, "fault timer"); return; }
	if (s->faults & (CHARGER_FAULT_NTC_HOT | CHARGER_FAULT_NTC_COLD))
	                                        { put(buf, n, "fault ntc");   return; }
	if (s->watchdog_fault)                  { put(buf, n, "fault wdog");  return; }

	if (!s->power_good) {
		put(buf, n, "battery");
		return;
	}

	switch (s->state) {
	case CHARGER_PRECHARGE: put(buf, n, "USB pre");  break;
	case CHARGER_CHARGING:  put(buf, n, "USB chg");  break;
	case CHARGER_DONE:      put(buf, n, "USB done"); break;
	default:                put(buf, n, "USB idle"); break;
	}
}

const char *status_text(void)
{
	static char buf[BTHOME_STATUS_TEXT_MAX + 1];
	charger_status_t s = { 0 };
	int present = charger_present() && charger_read(&s);

	status_format(buf, sizeof(buf), present, &s);
	return buf;
}
