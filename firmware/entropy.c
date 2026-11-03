/*
 * Bindkey generation. See entropy.h.
 */

#include "board.h"
#include "entropy.h"
#include "ccm.h"
#include <string.h>

/*
 * Fixed conditioning key. Public and constant on purpose: CBC-MAC under a
 * known key is a deterministic compressor, which is all conditioning
 * needs. The secrecy comes from the pool.
 */
static const uint8_t cond_key[16] = {
	'L', 'e', '\'', 'F', 'o', 'b', ' ', 'e',
	'n', 't', 'r', 'o', 'p', 'y', ' ', '1',
};

/*
 * Fewer distinct byte values than this in either half means stuck: an ADC
 * that never moves, or two clocks that are secretly one. It is a tripwire
 * for a dead source, not a measure of quality -- that is the job of the
 * batch audit in the MVP spec (section 17), on real boards.
 */
#define ENTROPY_MIN_DISTINCT 2

#define ENTROPY_ATTEMPTS 3

void entropy_condition(const uint8_t *pool, int len, const uint8_t mac[6],
                       uint8_t key[16])
{
	uint8_t mix[16];

	/* MAC first, as a block of its own, then the whole pool chained on. */
	memset(mix, 0, sizeof(mix));
	memcpy(mix, mac, 6);
	cbc_mac(cond_key, mix, 16, mix);

	/* Feed the MAC result in as the key for the pool pass, so the two are
	 * bound together rather than merely concatenated. */
	cbc_mac(mix, pool, len, key);
}

static int distinct(const uint8_t *p, int n)
{
	uint8_t seen[32] = { 0 };
	int     count = 0;

	for (int i = 0; i < n; i++) {
		uint8_t bit = (uint8_t)(1u << (p[i] & 7));

		if (!(seen[p[i] >> 3] & bit)) {
			seen[p[i] >> 3] |= bit;
			count++;
		}
	}
	return count;
}

int entropy_healthy(const uint8_t pool[ENTROPY_POOL_LEN])
{
	return distinct(pool, ENTROPY_ADC_SAMPLES) >= ENTROPY_MIN_DISTINCT &&
	       distinct(pool + ENTROPY_ADC_SAMPLES, ENTROPY_BEAT_SAMPLES) >=
	               ENTROPY_MIN_DISTINCT;
}

#ifndef LEFOB_HOST_TEST

#include "ch32fun.h"

#define ADC_CH_VTEMP 15
#define ADC_SPIN     2000u

static void fill_pool(uint8_t pool[ENTROPY_POOL_LEN])
{
	/*
	 * Temperature sensor, exactly temp.c's register recipe: a setting
	 * known not to saturate, so the low bits move with noise rather than
	 * sitting pinned at full scale.
	 */
	R8_TKEY_CFG    &= ~RB_TKEY_PWR_ON;
	R8_TEM_SENSOR   = RB_TEM_SEN_PWR_ON;
	R8_ADC_CHANNEL  = ADC_CH_VTEMP;
	R8_ADC_CFG      = RB_ADC_POWER_ON | RB_ADC_DIFF_EN | (3u << 4);
	R8_ADC_CONVERT &= ~RB_ADC_PGA_GAIN2;

	for (int i = -1; i < ENTROPY_ADC_SAMPLES; i++) {
		uint32_t n = ADC_SPIN;

		R8_ADC_CONVERT |= RB_ADC_START;
		while ((R8_ADC_CONVERT & RB_ADC_START) && --n)
			;
		/* The first conversion after power-up is discarded. Each kept
		 * sample folds the 12-bit result down to a byte, low bits
		 * dominant, so a stuck high byte cannot mask a live low one. */
		if (i >= 0) {
			uint16_t raw = R16_ADC_DATA & RB_ADC_DATA;
			pool[i] = (uint8_t)(raw ^ (raw >> 8));
		}
	}

	R8_ADC_CFG      = 0;
	R8_TEM_SENSOR   = 0;

	/*
	 * Clock beat: SysTick ticks between consecutive RTC edges. The nominal
	 * value is fixed (60 MHz / 32768 Hz, about 1831), so what the low byte
	 * carries is the jitter between the two crystals and the edge
	 * synchroniser -- the raw count itself would step by a constant and
	 * only look random.
	 */
	uint8_t *beat = pool + ENTROPY_ADC_SAMPLES;
	uint32_t last = 0;

	for (int i = -1; i < ENTROPY_BEAT_SAMPLES; i++) {
		uint16_t t = R16_RTC_CNT_32K;
		uint32_t n = 100000u;

		while (R16_RTC_CNT_32K == t && --n)
			;
		uint32_t s = SysTick->CNT;

		if (i >= 0)
			beat[i] = (uint8_t)(s - last);
		last = s;
	}
}

int entropy_generate_key(const uint8_t mac[6], uint8_t key[16])
{
	static uint8_t pool[ENTROPY_POOL_LEN];
	int            healthy = 0;

	for (int attempt = 0; attempt < ENTROPY_ATTEMPTS && !healthy; attempt++) {
		fill_pool(pool);
		healthy = entropy_healthy(pool);
	}

	entropy_condition(pool, ENTROPY_POOL_LEN, mac, key);
	memset(pool, 0, sizeof(pool));
	return healthy;
}

#endif /* LEFOB_HOST_TEST */
