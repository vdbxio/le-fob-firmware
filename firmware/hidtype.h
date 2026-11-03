#ifndef LE_FOB_HIDTYPE_H
#define LE_FOB_HIDTYPE_H

#include <stdint.h>

/*
 * Types the bindkey as a USB keyboard: exactly 32 lowercase hex
 * characters, nothing before or after -- no Enter, no prefix. Home
 * Assistant's BTHome bindkey field accepts precisely that and nothing else.
 *
 * Key choice is in board.h (HID_KEYPAD_DIGITS). This module only turns the
 * key into a timed sequence of 8-byte boot-keyboard reports; usb_hid.c
 * puts them on the wire. Pure, so the host tests can read the sequence.
 */

/* Usage IDs (HID Usage Tables, keyboard page). */
#define HID_KEY_A        0x04
#define HID_KEY_1        0x1E
#define HID_KEY_0        0x27
#define HID_KEY_NUMLOCK  0x53
#define HID_KEY_KP_1     0x59
#define HID_KEY_KP_0     0x62

/*
 * Start typing `key`. `numlock_off` is nonzero only if the host has told
 * us NumLock is off; then NumLock is pressed before and after, so the
 * keypad types digits and the host is left as it was found. Typing starts
 * HID_LEAD_MS after `now`.
 */
void hidtype_start(const uint8_t key[16], int numlock_off, uint32_t now);

/* Nonzero until the last key has been released. */
int hidtype_busy(void);

/*
 * If a report is due at `now`, write it to `report` and return 1. Call
 * only when the endpoint can take it: the sequence advances on each 1.
 */
int hidtype_poll(uint32_t now, uint8_t report[8]);

/* Stop and forget the key (USB went away mid-typing). */
void hidtype_abort(void);

/* The usage ID for one lowercase hex character, or 0. */
uint8_t hidtype_code_for(char c);

#endif /* LE_FOB_HIDTYPE_H */
