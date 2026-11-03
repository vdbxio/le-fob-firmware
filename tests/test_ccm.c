/*
 * Host-side check of firmware/ccm.c, the AES-CCM that BTHome encryption
 * rests on.
 *
 * Two layers, the second trusted only once the first passed:
 *
 *   1. the software AES stand-in, against FIPS-197 appendix C.1
 *   2. the BTHome v2 encryption example from bthome.io: key, MAC and frame
 *      exactly as published, the same bytes Le'Casa's decoder test uses,
 *      decrypted and re-encrypted, plus tamper and replay rejection
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/aes.h"
#include "../firmware/ccm.h"

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

static void unhex(uint8_t *out, const char *hex)
{
	while (hex[0] && hex[1]) {
		unsigned v;

		if (hex[0] == ' ') {
			hex++;
			continue;
		}
		sscanf(hex, "%2x", &v);
		*out++ = (uint8_t)v;
		hex += 2;
	}
}

int main(void)
{
	printf("test_ccm\n");

	/* 1. FIPS-197 C.1 */
	{
		uint8_t key[16], pt[16], want[16], got[16];

		unhex(key,  "000102030405060708090a0b0c0d0e0f");
		unhex(pt,   "00112233445566778899aabbccddeeff");
		unhex(want, "69c4e0d86a7b0430d8cdb78070b4c55a");
		aes128_encrypt(key, pt, got);
		check(memcmp(got, want, 16) == 0, "AES-128 FIPS-197 C.1");
	}

	/* 2. The BTHome example: decrypt what bthome.io publishes. */
	uint8_t key[16];
	unhex(key, "231d39c1d7cc1ab1aee224cd096db932");

	/* 54:48:E6:8F:80:A5 as the radio sends it, least significant first. */
	const uint8_t mac_ota[6] = { 0xA5, 0x80, 0x8F, 0xE6, 0x48, 0x54 };

	/* Service data: info 0x41, ciphertext, counter 00112233, MIC. */
	uint8_t frame[15];
	unhex(frame, "41 a4 72 66 c9 5f 73 00 11 22 33 78 23 72 14");

	uint8_t nonce[CCM_NONCE_LEN];
	uint32_t counter = (uint32_t)frame[7] | (uint32_t)frame[8] << 8 |
	                   (uint32_t)frame[9] << 16 | (uint32_t)frame[10] << 24;
	bthome_nonce(nonce, mac_ota, frame[0], counter);

	uint8_t want_nonce[13];
	unhex(want_nonce, "5448e68f80a5 d2fc 41 00112233");
	check(memcmp(nonce, want_nonce, 13) == 0, "nonce: MAC reversed, UUID, info, counter");

	uint8_t buf[6];
	memcpy(buf, frame + 1, 6);
	check(ccm_decrypt(key, nonce, buf, 6, frame + 11), "bthome.io frame: MIC verifies");

	uint8_t plain[6];
	unhex(plain, "02ca09 03bf13"); /* temperature 25.06 C, humidity 50.55 % */
	check(memcmp(buf, plain, 6) == 0, "bthome.io frame: decrypts to temp + humidity");

	/* 3. And the other way: encrypting the plaintext gives their bytes. */
	uint8_t mic[4];
	memcpy(buf, plain, 6);
	ccm_encrypt(key, nonce, buf, 6, mic);
	check(memcmp(buf, frame + 1, 6) == 0 && memcmp(mic, frame + 11, 4) == 0,
	      "encrypt reproduces the published ciphertext + MIC");

	/* Tamper with one bit anywhere: rejected. */
	{
		int all_rejected = 1;

		for (int bit = 0; bit < 6 * 8; bit++) {
			memcpy(buf, frame + 1, 6);
			buf[bit / 8] ^= (uint8_t)(1u << (bit % 8));
			if (ccm_decrypt(key, nonce, buf, 6, frame + 11))
				all_rejected = 0;
		}
		check(all_rejected, "every single-bit flip is rejected");
	}

	/* Counter is part of the nonce: same bytes, next counter, rejected. */
	bthome_nonce(nonce, mac_ota, frame[0], counter + 1);
	memcpy(buf, frame + 1, 6);
	check(!ccm_decrypt(key, nonce, buf, 6, frame + 11), "replayed under a different counter: rejected");

	/* Longer than one block: round-trips (the fob's frames reach ~17 bytes). */
	{
		uint8_t msg[23], copy[23];

		for (int i = 0; i < (int)sizeof(msg); i++)
			msg[i] = (uint8_t)(i * 7 + 3);
		memcpy(copy, msg, sizeof(msg));
		bthome_nonce(nonce, mac_ota, 0x45, 1000);
		ccm_encrypt(key, nonce, msg, sizeof(msg), mic);
		check(memcmp(msg, copy, sizeof(msg)) != 0, "two-block message changes under encryption");
		check(ccm_decrypt(key, nonce, msg, sizeof(msg), mic) &&
		      memcmp(msg, copy, sizeof(msg)) == 0,
		      "two-block message round-trips");
	}

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures != 0;
}
