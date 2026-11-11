/*
 * Bindkey typing sequence. See hidtype.h.
 */

#include "board.h"
#include "hidtype.h"
#include <string.h>

/* 32 characters plus NumLock on either side. */
#define MAX_KEYS 34

static uint8_t  codes[MAX_KEYS];
static int      n_codes;
static int      step;     /* even: press codes[step/2]; odd: release it */
static uint32_t next_at;

uint8_t hidtype_code_for(char c)
{
	if (c >= '1' && c <= '9')
		return (uint8_t)((HID_KEYPAD_DIGITS ? HID_KEY_KP_1 : HID_KEY_1) + (c - '1'));
	if (c == '0')
		return HID_KEYPAD_DIGITS ? HID_KEY_KP_0 : HID_KEY_0;
	if (c >= 'a' && c <= 'f')
		return (uint8_t)(HID_KEY_A + (c - 'a'));
	return 0;
}

void hidtype_start(const uint8_t key[16], int numlock_off, uint32_t now)
{
	static const char hex[] = "0123456789abcdef";
	int               toggle = HID_KEYPAD_DIGITS && numlock_off;

	n_codes = 0;
	if (toggle)
		codes[n_codes++] = HID_KEY_NUMLOCK;
	for (int i = 0; i < 16; i++) {
		codes[n_codes++] = hidtype_code_for(hex[key[i] >> 4]);
		codes[n_codes++] = hidtype_code_for(hex[key[i] & 15]);
	}
	if (toggle)
		codes[n_codes++] = HID_KEY_NUMLOCK;

	step    = 0;
	next_at = now + HID_LEAD_MS;
}

int hidtype_busy(void)
{
	return step < 2 * n_codes;
}

int hidtype_poll(uint32_t now, uint8_t report[8])
{
	if (!hidtype_busy() || (int32_t)(now - next_at) < 0)
		return 0;

	memset(report, 0, 8);
	if (!(step & 1))
		report[2] = codes[step / 2];

	step++;
	next_at = now + HID_KEY_MS;

	/* Once the last release is out, nothing of the key stays in RAM. */
	if (!hidtype_busy())
		hidtype_abort();
	return 1;
}

void hidtype_abort(void)
{
	memset(codes, 0, sizeof(codes));
	n_codes = 0;
	step    = 0;
}
