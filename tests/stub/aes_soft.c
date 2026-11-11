/*
 * Software AES-128 encryption for the host tests, standing in for the
 * CH592's AES block (firmware/aes_hw.c). Textbook FIPS-197, written for
 * clarity rather than speed; test_ccm checks it against the FIPS-197
 * appendix C.1 vector before trusting it with anything else.
 */

#include <stdint.h>
#include <string.h>
#include "../../firmware/aes.h"

static uint8_t sbox[256];

static uint8_t xtime(uint8_t x)
{
	return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0));
}

static uint8_t gmul(uint8_t a, uint8_t b)
{
	uint8_t p = 0;

	while (b) {
		if (b & 1)
			p ^= a;
		a = xtime(a);
		b >>= 1;
	}
	return p;
}

/* Build the S-box from its definition (inverse in GF(2^8) + affine map)
 * instead of pasting a 256-entry table that could hide a typo. */
static void sbox_init(void)
{
	if (sbox[0] == 0x63)
		return;

	for (int x = 0; x < 256; x++) {
		uint8_t inv = 0;

		for (int y = 1; y < 256 && x; y++)
			if (gmul((uint8_t)x, (uint8_t)y) == 1) {
				inv = (uint8_t)y;
				break;
			}

		uint8_t s = inv;
		for (int r = 1; r <= 4; r++)
			s ^= (uint8_t)((inv << r) | (inv >> (8 - r)));
		sbox[x] = (uint8_t)(s ^ 0x63);
	}
}

void aes128_encrypt(const uint8_t key[16], const uint8_t in[16],
                    uint8_t out[16])
{
	uint8_t rk[176], st[16], t[16];
	uint8_t rcon = 1;

	sbox_init();

	memcpy(rk, key, 16);
	for (int i = 16; i < 176; i += 4) {
		uint8_t w[4] = { rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1] };

		if (i % 16 == 0) {
			uint8_t w0 = w[0];
			w[0] = (uint8_t)(sbox[w[1]] ^ rcon);
			w[1] = sbox[w[2]];
			w[2] = sbox[w[3]];
			w[3] = sbox[w0];
			rcon = xtime(rcon);
		}
		for (int j = 0; j < 4; j++)
			rk[i + j] = (uint8_t)(rk[i - 16 + j] ^ w[j]);
	}

	for (int i = 0; i < 16; i++)
		st[i] = (uint8_t)(in[i] ^ rk[i]);

	for (int round = 1; round <= 10; round++) {
		/* SubBytes + ShiftRows (state is column-major) */
		for (int c = 0; c < 4; c++)
			for (int r = 0; r < 4; r++)
				t[c * 4 + r] = sbox[st[((c + r) % 4) * 4 + r]];

		/* MixColumns, except in the last round */
		if (round != 10) {
			for (int c = 0; c < 4; c++) {
				uint8_t *col = t + c * 4;
				uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];

				col[0] = (uint8_t)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
				col[1] = (uint8_t)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
				col[2] = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
				col[3] = (uint8_t)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
			}
		}

		for (int i = 0; i < 16; i++)
			st[i] = (uint8_t)(t[i] ^ rk[round * 16 + i]);
	}

	memcpy(out, st, 16);
}
