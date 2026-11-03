/*
 * AES-CCM for BTHome v2. See ccm.h. Straight RFC 3610, sized for frames
 * that never exceed two blocks, so nothing here allocates or loops long.
 */

#include "ccm.h"
#include "aes.h"
#include <string.h>

/* RFC 3610 flags: Adata = 0, M' = (M - 2) / 2 = 1, L' = L - 1 = 1. */
#define CCM_FLAGS_B0 ((((CCM_MIC_LEN - 2) / 2) << 3) | 1)
#define CCM_FLAGS_A  1

static void counter_block(uint8_t a[16], const uint8_t nonce[CCM_NONCE_LEN],
                          uint16_t i)
{
	a[0] = CCM_FLAGS_A;
	memcpy(a + 1, nonce, CCM_NONCE_LEN);
	a[14] = (uint8_t)(i >> 8);
	a[15] = (uint8_t)i;
}

/* T = first M bytes of the CBC-MAC over B0 || message. */
static void ccm_tag(const uint8_t key[16], const uint8_t nonce[CCM_NONCE_LEN],
                    const uint8_t *msg, int len, uint8_t tag[CCM_MIC_LEN])
{
	uint8_t x[16];

	x[0] = CCM_FLAGS_B0;
	memcpy(x + 1, nonce, CCM_NONCE_LEN);
	x[14] = (uint8_t)(len >> 8);
	x[15] = (uint8_t)len;
	aes128_encrypt(key, x, x);

	for (int off = 0; off < len; off += 16) {
		for (int i = 0; i < 16 && off + i < len; i++)
			x[i] ^= msg[off + i];
		aes128_encrypt(key, x, x);
	}

	memcpy(tag, x, CCM_MIC_LEN);
}

/* XOR the CTR keystream (A1, A2, ...) into buf, and return S0 for the MIC. */
static void ccm_ctr(const uint8_t key[16], const uint8_t nonce[CCM_NONCE_LEN],
                    uint8_t *buf, int len, uint8_t s0[16])
{
	uint8_t a[16], s[16];

	counter_block(a, nonce, 0);
	aes128_encrypt(key, a, s0);

	for (int off = 0, blk = 1; off < len; off += 16, blk++) {
		counter_block(a, nonce, (uint16_t)blk);
		aes128_encrypt(key, a, s);
		for (int i = 0; i < 16 && off + i < len; i++)
			buf[off + i] ^= s[i];
	}
}

void ccm_encrypt(const uint8_t key[16], const uint8_t nonce[CCM_NONCE_LEN],
                 uint8_t *buf, int len, uint8_t mic[CCM_MIC_LEN])
{
	uint8_t t[CCM_MIC_LEN], s0[16];

	ccm_tag(key, nonce, buf, len, t);
	ccm_ctr(key, nonce, buf, len, s0);
	for (int i = 0; i < CCM_MIC_LEN; i++)
		mic[i] = t[i] ^ s0[i];
}

int ccm_decrypt(const uint8_t key[16], const uint8_t nonce[CCM_NONCE_LEN],
                uint8_t *buf, int len, const uint8_t mic[CCM_MIC_LEN])
{
	uint8_t t[CCM_MIC_LEN], s0[16];
	uint8_t diff = 0;

	ccm_ctr(key, nonce, buf, len, s0);
	ccm_tag(key, nonce, buf, len, t);
	for (int i = 0; i < CCM_MIC_LEN; i++)
		diff |= (uint8_t)(t[i] ^ s0[i] ^ mic[i]);
	return diff == 0;
}

void bthome_nonce(uint8_t nonce[CCM_NONCE_LEN], const uint8_t mac_ota[6],
                  uint8_t device_info, uint32_t counter)
{
	/* BTHome's nonce wants the address the way a human writes it. */
	for (int i = 0; i < 6; i++)
		nonce[i] = mac_ota[5 - i];
	nonce[6]  = 0xD2;
	nonce[7]  = 0xFC;
	nonce[8]  = device_info;
	nonce[9]  = (uint8_t)counter;
	nonce[10] = (uint8_t)(counter >> 8);
	nonce[11] = (uint8_t)(counter >> 16);
	nonce[12] = (uint8_t)(counter >> 24);
}

void cbc_mac(const uint8_t key[16], const uint8_t *data, int len,
             uint8_t out[16])
{
	uint8_t x[16] = { 0 };

	for (int off = 0; off < len; off += 16) {
		for (int i = 0; i < 16 && off + i < len; i++)
			x[i] ^= data[off + i];
		aes128_encrypt(key, x, x);
	}
	memcpy(out, x, 16);
}
