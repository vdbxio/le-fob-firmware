#ifndef LE_FOB_USB_HID_H
#define LE_FOB_USB_HID_H

#include <stdint.h>

/*
 * USB keyboard for the bindkey readout, on ch32fun's fsusb stack.
 *
 * The USB block only runs while the charger says VBUS is present: the main
 * loop starts it on plug-in and stops it on unplug, so it costs nothing on
 * battery and never stands between the fob and deep sleep (which only
 * happens off USB anyway).
 */

void usb_hid_start(void);
void usb_hid_stop(void);

/* Nonzero once the host has configured us: keys typed now will land. */
int usb_hid_ready(void);

/* Begin typing `key` (see hidtype.h). */
void usb_hid_type_key(const uint8_t key[16], uint32_t now);

/* Nonzero while typing is in progress. */
int usb_hid_busy(void);

/* Feed reports to the endpoint as they fall due. Call every tick. */
void usb_hid_tick(uint32_t now);

#endif /* LE_FOB_USB_HID_H */
