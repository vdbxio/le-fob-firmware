#ifndef LE_FOB_ENTROPY_H
#define LE_FOB_ENTROPY_H

#include <stdint.h>

/*
 * Bindkey generation from on-chip noise (MVP spec section 10).
 *
 * The pool is filled from two sources that owe nothing to each other:
 *
 *   ADC noise    raw conversions of the internal temperature sensor.
 *                The low bits wander with thermal noise.
 *   clock beat   SysTick (32 MHz crystal through the PLL) sampled on
 *                every tick of the RTC (32.768 kHz crystal). Two
 *                free-running crystals drift and jitter against each
 *                other, so the count between RTC edges is not repeatable.
 *
 * The pool is then conditioned through AES-CBC-MAC on the hardware AES
 * block, with the factory MAC mixed in as a diversifier. The MAC is not
 * secret and adds no strength; it is there so that two fobs can never end
 * up with the same key even if every noise source came back identical --
 * which turns "a whole batch shares one key" into "distinct keys, possibly
 * weak", a firmware update instead of a recall.
 *
 * Not used: RF noise via the radio's RSSI, which the spec also lists. It
 * needs the radio held in receive with iSLER, and the two sources above
 * are cheap and independent. It can be added to the pool later without
 * changing anything else.
 */

#define ENTROPY_ADC_SAMPLES  512
#define ENTROPY_BEAT_SAMPLES 256
#define ENTROPY_POOL_LEN     (ENTROPY_ADC_SAMPLES + ENTROPY_BEAT_SAMPLES)

/*
 * Fill a pool and condition it into `key`. Takes roughly 10 ms. Returns 1
 * if the pool passed its health check, 0 if it looked stuck even after
 * retrying -- the key is still written (and still unique, thanks to the
 * MAC), but the caller should say so loudly.
 */
int entropy_generate_key(const uint8_t mac[6], uint8_t key[16]);

/*
 * The pure parts, for the host tests.
 *
 * entropy_condition(): pool + MAC -> 16-byte key.
 * entropy_healthy(): rejects a pool whose ADC half or clock half never
 * changes value.
 */
void entropy_condition(const uint8_t *pool, int len, const uint8_t mac[6],
                       uint8_t key[16]);
int  entropy_healthy(const uint8_t pool[ENTROPY_POOL_LEN]);

#endif /* LE_FOB_ENTROPY_H */
