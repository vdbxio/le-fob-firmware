#ifndef _USB_CONFIG_H
#define _USB_CONFIG_H

/*
 * ch32fun fsusb configuration for Le'Fob: one boot-protocol keyboard
 * interface, used only to type the bindkey out (usb_hid.c). fsusb.c
 * includes this file and serves the descriptors below.
 */

#include "funconfig.h"
#include "ch32fun.h"

#define FUSB_BUFFERS_NUMBER   2 /* EP0 + EP1 IN */
#define FUSB_EP1_MODE         USBFS_EP_MODE_TX
#define FUSB_SUPPORTS_SLEEP   0
#define FUSB_HID_INTERFACES   1
#define FUSB_CURSED_TURBO_DMA 0
#define FUSB_HID_USER_REPORTS 1
#define FUSB_IO_PROFILE       0
#define FUSB_USE_HPE          0
#define FUSB_USER_HANDLERS    0
#define FUSB_USE_DMA7_COPY    0
#define FUSB_VDD_5V           0
#define FUSB_FROM_RAM         0

#include "usb_defines.h"

/*
 * pid.codes test VID/PID, as ch32fun's examples use. Fine for a handful of
 * units; a production run wants a PID of its own.
 */
#define FUSB_USB_VID 0x1209
#define FUSB_USB_PID 0xd035
#define FUSB_USB_REV 0x0100
#define FUSB_STR_MANUFACTURER u"Voidbox Industries"
#define FUSB_STR_PRODUCT      u"Le'Fob"
#define FUSB_STR_SERIAL       u"0"

static const uint8_t device_descriptor[] = {
	18,         /* bLength */
	1,          /* bDescriptorType: device */
	0x00, 0x02, /* bcdUSB 2.00 */
	0, 0, 0,    /* class from the interface */
	64,         /* bMaxPacketSize0 */
	(uint8_t)FUSB_USB_VID, (uint8_t)(FUSB_USB_VID >> 8),
	(uint8_t)FUSB_USB_PID, (uint8_t)(FUSB_USB_PID >> 8),
	(uint8_t)FUSB_USB_REV, (uint8_t)(FUSB_USB_REV >> 8),
	1, 2, 3,    /* manufacturer, product, serial strings */
	1,          /* bNumConfigurations */
};

/* Standard boot keyboard: modifiers, reserved, six keys in; five LEDs out
 * (bit 0 is NumLock, which the typing code reads). */
static const uint8_t KeyRepDesc[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,       /* Generic Desktop, Keyboard, Application */
	0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,       /* modifier keys */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x95, 0x01, 0x75, 0x08, 0x81, 0x01,       /* reserved byte */
	0x95, 0x05, 0x75, 0x01, 0x05, 0x08,       /* LEDs out */
	0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
	0x95, 0x01, 0x75, 0x03, 0x91, 0x01,       /* LED padding */
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00,
	0x05, 0x07, 0x19, 0x00, 0x29, 0xFF, 0x81, 0x00, /* six key slots */
	0xC0,
};

static const uint8_t config_descriptor[] = {
	9, 2, 34, 0, /* configuration, wTotalLength 34 */
	1,           /* one interface */
	1, 0,        /* configuration value, no string */
	0x80,        /* bus powered */
	50,          /* 100 mA */

	9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0, /* interface 0: HID, boot, keyboard */

	9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(KeyRepDesc), 0, /* HID 1.11 */

	7, 5, 0x81, 0x03, 8, 0, 10, /* EP1 IN, interrupt, 8 bytes, 10 ms */
};

struct usb_string_descriptor_struct {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t wString[];
};
const static struct usb_string_descriptor_struct string0 __attribute__((section(".rodata"))) = {
	4, 3, { 0x0409 }
};
const static struct usb_string_descriptor_struct string1 __attribute__((section(".rodata"))) = {
	sizeof(FUSB_STR_MANUFACTURER), 3, FUSB_STR_MANUFACTURER
};
const static struct usb_string_descriptor_struct string2 __attribute__((section(".rodata"))) = {
	sizeof(FUSB_STR_PRODUCT), 3, FUSB_STR_PRODUCT
};
const static struct usb_string_descriptor_struct string3 __attribute__((section(".rodata"))) = {
	sizeof(FUSB_STR_SERIAL), 3, FUSB_STR_SERIAL
};

/* Which descriptor answers which GET_DESCRIPTOR (wIndex << 16 | wValue). */
const static struct descriptor_list_struct {
	uint32_t       lIndexValue;
	const uint8_t *addr;
	uint8_t        length;
} descriptor_list[] = {
	{ 0x00000100, device_descriptor, sizeof(device_descriptor) },
	{ 0x00000200, config_descriptor, sizeof(config_descriptor) },
	{ 0x00002200, KeyRepDesc, sizeof(KeyRepDesc) },
	{ 0x00002100, config_descriptor + 18, 9 }, /* HID descriptor */
	{ 0x00000300, (const uint8_t *)&string0, 4 },
	{ 0x04090301, (const uint8_t *)&string1, sizeof(FUSB_STR_MANUFACTURER) },
	{ 0x04090302, (const uint8_t *)&string2, sizeof(FUSB_STR_PRODUCT) },
	{ 0x04090303, (const uint8_t *)&string3, sizeof(FUSB_STR_SERIAL) },
};
#define DESCRIPTOR_LIST_ENTRIES (sizeof(descriptor_list) / sizeof(descriptor_list[0]))

#endif /* _USB_CONFIG_H */
