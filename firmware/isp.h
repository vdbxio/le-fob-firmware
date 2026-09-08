#ifndef LE_FOB_ISP_H
#define LE_FOB_ISP_H

#include <stdint.h>

/*
 * Bootloader entry from the running application.
 *
 * The ROM bootloader samples the BOOT pin (SW1 / PB22) only at reset. With
 * no battery fitted, plugging USB *is* a reset, so hold-SW1-and-plug works.
 * With a battery fitted the chip never loses power, USB does not reset it,
 * and the hold does nothing: the app just sees button 1 go down. That is
 * not the app blocking anything; it is the reset that never happened.
 *
 * So the app provides its own way in: hold SW1 alone for ISP_HOLD_MS and it
 * jumps to the bootloader itself. The pads fill in one by one over the
 * last part of the hold so the user can see it arming and let go if it was
 * not what they meant.
 *
 * This file is the timing only, kept free of hardware so it can be run on
 * the host. The actual jump lives in lefob.c.
 */

/*
 * Feed the debounced pad mask every tick. Returns 1 exactly once, on the
 * tick SW1 (alone) has been held for ISP_HOLD_MS; then never again until
 * every pad has been released.
 */
int isp_hold_tick(uint8_t pressed_mask, uint32_t now);

/*
 * 0 while not arming, otherwise 1..255 as the hold runs from ISP_ARM_MS up
 * to ISP_HOLD_MS. Drives the LED "fuse".
 */
uint8_t isp_hold_progress(void);

#endif /* LE_FOB_ISP_H */
