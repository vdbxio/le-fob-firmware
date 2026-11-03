/*
 * Host-side check of the pure half of firmware/entropy.c: conditioning
 * and the stuck-source tripwire. The noise sources themselves can only be
 * judged on real boards (MVP spec section 17's batch audit).
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/entropy.h"

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

int main(void)
{
	static uint8_t pool[ENTROPY_POOL_LEN];
	uint8_t mac_a[6] = { 0x11, 0x22, 0x33, 0xE4, 0xC2, 0x84 };
	uint8_t mac_b[6] = { 0x12, 0x22, 0x33, 0xE4, 0xC2, 0x84 };
	uint8_t k1[16], k2[16];

	printf("test_entropy\n");

	for (int i = 0; i < ENTROPY_POOL_LEN; i++)
		pool[i] = (uint8_t)(i * 37 + (i >> 3));

	entropy_condition(pool, ENTROPY_POOL_LEN, mac_a, k1);
	entropy_condition(pool, ENTROPY_POOL_LEN, mac_a, k2);
	check(memcmp(k1, k2, 16) == 0, "deterministic: same pool + MAC, same key");

	/* The failure the MAC exists for: every fob's noise came back the
	 * same. Keys must still differ. */
	entropy_condition(pool, ENTROPY_POOL_LEN, mac_b, k2);
	check(memcmp(k1, k2, 16) != 0, "identical pools, different MACs: different keys");

	{
		int all_differ = 1;

		for (int i = 0; i < ENTROPY_POOL_LEN; i += 13) {
			pool[i] ^= 1;
			entropy_condition(pool, ENTROPY_POOL_LEN, mac_a, k2);
			if (memcmp(k1, k2, 16) == 0)
				all_differ = 0;
			pool[i] ^= 1;
		}
		check(all_differ, "one pool bit flipped anywhere: key changes");
	}

	{
		static uint8_t zero[ENTROPY_POOL_LEN];

		entropy_condition(zero, ENTROPY_POOL_LEN, mac_a, k2);
		int nonzero = 0;
		for (int i = 0; i < 16; i++)
			nonzero |= k2[i];
		check(nonzero, "all-zero pool still yields a non-zero key");
	}

	check(entropy_healthy(pool), "varied pool: healthy");

	memset(pool, 0x5A, ENTROPY_ADC_SAMPLES);
	check(!entropy_healthy(pool), "ADC half stuck on one value: unhealthy");

	for (int i = 0; i < ENTROPY_POOL_LEN; i++)
		pool[i] = (uint8_t)(i * 37 + (i >> 3));
	memset(pool + ENTROPY_ADC_SAMPLES, 0x27, ENTROPY_BEAT_SAMPLES);
	check(!entropy_healthy(pool), "clock half stuck on one value: unhealthy");

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures != 0;
}
