#ifndef LE_FOB_CCM_H
#define LE_FOB_CCM_H

#include <stdint.h>

/*
 * AES-128-CCM exactly as BTHome v2 uses it (RFC 3610 with M = 4, L = 2):
 * 13-byte nonce, 4-byte MIC, no associated data, messages under 64 KB.
 *
 * BTHome's nonce is MAC (6, over-the-air order reversed: most significant
 * byte first) + UUID 0xFCD2 little-endian (2) + device-info byte (1) +
 * counter (4, little-endian). bthome_nonce() builds it.
 */

#define CCM_NONCE_LEN 13
#define CCM_MIC_LEN   4

/* Encrypt `len` bytes of `buf` in place and write the 4-byte MIC. */
void ccm_encrypt(const uint8_t key[16], const uint8_t nonce[CCM_NONCE_LEN],
                 uint8_t *buf, int len, uint8_t mic[CCM_MIC_LEN]);

/*
 * Decrypt in place and check the MIC. Returns 1 if it matched; on a
 * mismatch returns 0 and `buf` holds garbage. The fob itself never
 * decrypts -- this is for the tests and for anything that wants to check a
 * frame it built.
 */
int ccm_decrypt(const uint8_t key[16], const uint8_t nonce[CCM_NONCE_LEN],
                uint8_t *buf, int len, const uint8_t mic[CCM_MIC_LEN]);

/*
 * BTHome nonce from the advertising address as the radio sends it (least
 * significant byte first, i.e. ble_adv_mac()), the device-info byte and
 * the counter.
 */
void bthome_nonce(uint8_t nonce[CCM_NONCE_LEN], const uint8_t mac_ota[6],
                  uint8_t device_info, uint32_t counter);

/*
 * AES-CBC-MAC of `len` bytes (zero-padded to a whole block) under `key`.
 * Used to condition the entropy pool, not for message authentication: on
 * its own, CBC-MAC over variable-length input is not a secure MAC.
 */
void cbc_mac(const uint8_t key[16], const uint8_t *data, int len,
             uint8_t out[16]);

#endif /* LE_FOB_CCM_H */
