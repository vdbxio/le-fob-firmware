#ifndef LE_FOB_AES_H
#define LE_FOB_AES_H

#include <stdint.h>

/*
 * One AES-128 block encryption: out = E(key, in). `out` may alias `in`.
 *
 * On the fob this is the CH592's hardware AES block (aes_hw.c), which
 * turns out to be plain FIPS-197 AES once the key and data are loaded as
 * little-endian words: ch32fun's AES example decrypts its "secret
 * message" to "wch+ghidra=love." under stock OpenSSL with no byte
 * shuffling. The host tests link a software AES instead
 * (tests/stub/aes_soft.c), so ccm.c is checked against published vectors
 * without the chip.
 *
 * Only the encrypt direction exists: CCM never needs the inverse cipher.
 */
void aes128_encrypt(const uint8_t key[16], const uint8_t in[16],
                    uint8_t out[16]);

#endif /* LE_FOB_AES_H */
