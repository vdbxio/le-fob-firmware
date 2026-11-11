/*
 * USB keyboard glue between fsusb and hidtype.c. See usb_hid.h.
 */

#include "ch32fun.h"
#include "fsusb.h"
#include "usb_hid.h"
#include "hidtype.h"
#include <string.h>

static uint8_t running;

/*
 * The host's LED output report: bit 0 NumLock. Written by the control
 * transfer below. `led_known` stays 0 until a host actually sends one --
 * macOS, for one, may never -- and then NumLock is assumed on.
 */
static volatile uint8_t led_report;
static volatile uint8_t led_known;

void usb_hid_start(void)
{
	if (running)
		return;
	led_known = 0;
	USBFSSetup();
	running = 1;
}

void usb_hid_stop(void)
{
	if (!running)
		return;
	hidtype_abort();
	USBFSReset();
	running = 0;
}

int usb_hid_ready(void)
{
	return running && USBFSCTX.USBFS_DevEnumStatus;
}

void usb_hid_type_key(const uint8_t key[16], uint32_t now)
{
	hidtype_start(key, led_known && !(led_report & 0x01), now);
}

int usb_hid_busy(void)
{
	return hidtype_busy();
}

void usb_hid_tick(uint32_t now)
{
	if (!running)
		return;
	if (!usb_hid_ready()) {
		hidtype_abort();
		return;
	}
	if (!hidtype_busy())
		return;

	uint8_t *buf = USBFS_GetEPBufferIfAvailable(1);
	uint8_t  report[8];

	if (buf && hidtype_poll(now, report)) {
		memcpy(buf, report, 8);
		USBFS_SendEndpoint(1, 8);
	}
}

/* ------------------------------------------------------------------ */
/* fsusb callbacks (FUSB_HID_USER_REPORTS)                            */
/* ------------------------------------------------------------------ */

int HandleHidUserSetReportSetup(struct _USBState *ctx, tusb_control_request_t *req)
{
	/* Output report, id 0: the keyboard LEDs. One byte. */
	if ((req->wValue >> 8) == 0x02 && req->wLength >= 1) {
		ctx->pCtrlPayloadPtr = (uint8_t *)&led_report;
		led_known            = 1;
		return 1;
	}
	return 0;
}

int HandleHidUserGetReportSetup(struct _USBState *ctx, tusb_control_request_t *req)
{
	(void)ctx;
	(void)req;
	return 0; /* nothing to report over the control pipe */
}

void HandleHidUserReportDataOut(struct _USBState *ctx, uint8_t *data, int len)
{
	(void)ctx;
	/* Some hosts send the LED byte without a pointer set up first. */
	if (len >= 1) {
		led_report = data[0];
		led_known  = 1;
	}
}

int HandleHidUserReportDataIn(struct _USBState *ctx, uint8_t *data, int len)
{
	(void)ctx;
	(void)data;
	return len;
}

void HandleHidUserReportOutComplete(struct _USBState *ctx)
{
	(void)ctx;
}
