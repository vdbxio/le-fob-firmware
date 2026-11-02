#include <string.h>
#include "board.h"
#include "battery.h"
#include "bthome.h"
#include "temp.h"
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

void status_format_absent(char *buf, size_t n, int driver_enabled,
                          charger_result_t why)
{
	if (!driver_enabled) {
		put(buf, n, "chgr off");   /* compiled out: not a fault */
		return;
	}
	switch (why) {
	case CHARGER_NO_ACK:     put(buf, n, "chgr nak");  break; /* bus / pull-ups */
	case CHARGER_BAD_ID:     put(buf, n, "chgr id?");  break; /* wrong part */
	case CHARGER_CFG_FAILED: put(buf, n, "chgr cfg!"); break; /* writes did not verify */
	default:                 put(buf, n, "chgr ?");    break;
	}
}

void status_format(char *buf, size_t n, int present, const charger_status_t *s)
{
	if (!present || !s->valid) {
		/* Caller should prefer status_format_absent() to say why; this
		 * is the fallback when a present part fails a status read. */
		put(buf, n, "chgr rd!");
		return;
	}

	/* A fault outranks everything: it is the thing to go and look at. */
	if (s->faults & CHARGER_FAULT_THERMAL)  { put(buf, n, "fault hot"); return; }
	if (s->faults & CHARGER_FAULT_BAT)      { put(buf, n, "fault bat");   return; }
	if (s->faults & CHARGER_FAULT_VIN)      { put(buf, n, "fault vin");   return; }
	if (s->faults & CHARGER_FAULT_TIMER)    { put(buf, n, "fault tmr"); return; }
	if (s->faults & (CHARGER_FAULT_NTC_HOT | CHARGER_FAULT_NTC_COLD))
	                                        { put(buf, n, "fault ntc");   return; }
	if (s->watchdog_fault)                  { put(buf, n, "fault wd");  return; }

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

static battery_state_t batt;

int status_battery_low(void)
{
	return batt.low;
}

void status_collect(bthome_status_t *out)
{
	static char            buf[BTHOME_STATUS_TEXT_MAX + 1];
	charger_status_t       s      = { 0 };
	int                    on_usb = 0;

	out->charging = 0;

	if (!FOB_ENABLE_CHARGER || !charger_present()) {
		status_format_absent(buf, sizeof(buf), FOB_ENABLE_CHARGER,
		                     charger_result());
	} else {
		int ok = charger_read(&s);

		status_format(buf, sizeof(buf), ok, &s);
		if (ok) {
			on_usb        = s.power_good;
			out->charging = s.power_good &&
			                (s.state == CHARGER_PRECHARGE ||
			                 s.state == CHARGER_CHARGING);
		}
	}

	out->text      = buf;
	out->temp_c100 = temp_read_c100();
	out->supply_mv = battery_read_mv();
	out->batt_low  = (uint8_t)battery_low_update(&batt, out->supply_mv, on_usb);
}
