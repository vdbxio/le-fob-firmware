/*
 * DataFlash journal for the bindkey, the replay-counter reservation and
 * the exposure count. See keystore.h for the scheme.
 */

#include "board.h"
#include "keystore.h"
#include "dataflash.h"
#include <string.h>

#define KS_RECORD_SIZE   32u
#define KS_PER_PAGE      (DATAFLASH_PAGE_SIZE / KS_RECORD_SIZE)
#define KS_SLOTS         (KS_PAGES * KS_PER_PAGE)
#define KS_VERSION       1u
#define KS_SEQ_ERASED    0xFFFFFFFFu

/* The counter stops this far short of 2^32 rather than wrap. */
#define KS_COUNTER_CEILING 0xFFFFFF00u

/*
 * On-flash layout, little-endian, 32 bytes:
 *
 *    0  seq       uint32  increases by one per record written
 *    4  key       16 bytes
 *   20  limit     uint32  counter values in use are all below this
 *   24  exposure  uint16
 *   26  version   uint8
 *   27  reserved  uint8   0xFF
 *   28  crc       uint32  CRC-32 of bytes 0..27
 *
 * Packed by hand, not by struct, so the layout does not depend on the
 * compiler and the host tests see exactly the bytes the fob writes.
 */
typedef struct {
	uint32_t seq;
	uint8_t  key[16];
	uint32_t limit;
	uint16_t exposure;
} ks_record_t;

static ks_record_t cur;        /* the record in charge */
static int         have_cur;
static int         cur_slot = -1;
static uint32_t    next_ctr;   /* next counter value to hand out */

static uint32_t crc32(const uint8_t *p, int n)
{
	uint32_t c = 0xFFFFFFFFu;

	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[3] << 24;
}

static void pack(uint8_t b[KS_RECORD_SIZE], const ks_record_t *r)
{
	put32(b, r->seq);
	memcpy(b + 4, r->key, 16);
	put32(b + 20, r->limit);
	b[24] = (uint8_t)r->exposure;
	b[25] = (uint8_t)(r->exposure >> 8);
	b[26] = KS_VERSION;
	b[27] = 0xFF;
	put32(b + 28, crc32(b, 28));
}

static int unpack(const uint8_t b[KS_RECORD_SIZE], ks_record_t *r)
{
	r->seq = get32(b);
	if (r->seq == KS_SEQ_ERASED || b[26] != KS_VERSION ||
	    get32(b + 28) != crc32(b, 28))
		return 0;

	memcpy(r->key, b + 4, 16);
	r->limit    = get32(b + 20);
	r->exposure = (uint16_t)(b[24] | b[25] << 8);
	return 1;
}

static uint32_t slot_off(int slot)
{
	return KS_FLASH_OFFSET + (uint32_t)slot * KS_RECORD_SIZE;
}

/*
 * Append `r` (seq filled in here) to the next slot. Erases a page only when
 * the write is about to enter it, and that page can only hold records older
 * than the one in charge, which sits in the page before. If the write
 * fails, `cur` is untouched and still describes the last good record.
 */
static int append(ks_record_t *r)
{
	uint8_t b[KS_RECORD_SIZE];
	int     slot = cur_slot;

	r->seq = have_cur ? cur.seq + 1 : 1;
	pack(b, r);

	/*
	 * A slot that fails is skipped, not retried: it may hold the remains
	 * of a write a power cut tore, and a half-programmed slot can never be
	 * written again until its page is erased. A few attempts, then give
	 * up and let the caller refuse.
	 */
	for (int attempt = 0; attempt < KS_WRITE_ATTEMPTS; attempt++) {
		slot = (slot + 1) % (int)KS_SLOTS;

		if (slot % (int)KS_PER_PAGE == 0 && !df_erase_page(slot_off(slot)))
			continue;
		if (!df_write(slot_off(slot), b, KS_RECORD_SIZE))
			continue;

		cur      = *r;
		cur_slot = slot;
		have_cur = 1;
		return 1;
	}
	return 0;
}

static int all_erased(const uint8_t *b, int n)
{
	while (n--)
		if (*b++ != 0xFF)
			return 0;
	return 1;
}

ks_state_t keystore_open(void)
{
	uint32_t max_limit = 0;
	uint32_t damaged   = 0;

	have_cur = 0;
	cur_slot = -1;

	for (int s = 0; s < (int)KS_SLOTS; s++) {
		uint8_t     b[KS_RECORD_SIZE];
		ks_record_t r;

		df_read(slot_off(s), b, KS_RECORD_SIZE);
		if (!unpack(b, &r)) {
			if (!all_erased(b, KS_RECORD_SIZE))
				damaged++;
			continue;
		}

		/* Fail high: the counter resumes above the largest reservation
		 * any surviving record holds, not just the newest one's. */
		if (r.limit > max_limit)
			max_limit = r.limit;

		if (!have_cur || (int32_t)(r.seq - cur.seq) > 0) {
			cur      = r;
			cur_slot = s;
			have_cur = 1;
		}
	}

	/*
	 * Anything below the largest reservation may already have been sent.
	 *
	 * A damaged slot is a record we cannot read -- torn by a power cut
	 * before it verified, which is harmless, or a verified one that has
	 * since rotted, which is not: it may have held the newest reservation.
	 * No record ever reserves more than KS_RESERVE past the one before it,
	 * so skipping KS_RESERVE per damaged slot is always enough. That burns
	 * a little counter space per boot until the page is recycled; going
	 * low is the failure that matters.
	 */
	uint64_t resume = (uint64_t)max_limit + (uint64_t)damaged * KS_RESERVE;

	next_ctr = resume > KS_COUNTER_CEILING ? KS_COUNTER_CEILING
	                                       : (uint32_t)resume;

	if (!have_cur) {
		/* Start the next append at slot 0. */
		cur_slot = (int)KS_SLOTS - 1;
		return KS_EMPTY;
	}

	/* Run as if the window were spent; keystore_reserve() opens a new one. */
	cur.limit = next_ctr;
	return KS_OK;
}

int keystore_provision(const uint8_t key[16])
{
	ks_record_t r;

	memcpy(r.key, key, 16);
	r.exposure = 0;
	r.limit    = KS_COUNTER_START + KS_RESERVE;

	if (!append(&r))
		return 0;
	next_ctr = KS_COUNTER_START;
	return 1;
}

const uint8_t *keystore_key(void)
{
	return have_cur ? cur.key : 0;
}

static int extend(void)
{
	ks_record_t r = cur;
	uint64_t    lim = (uint64_t)next_ctr + KS_RESERVE;

	r.limit = lim > KS_COUNTER_CEILING ? KS_COUNTER_CEILING : (uint32_t)lim;
	if (r.limit <= cur.limit)
		return 0; /* at the ceiling: nothing left to reserve */
	return append(&r);
}

int keystore_reserve(void)
{
	if (!have_cur)
		return 0;
	if (next_ctr < KS_COUNTER_START)
		next_ctr = KS_COUNTER_START;
	return extend();
}

int keystore_next_counter(int may_write, uint32_t *out)
{
	if (!have_cur || next_ctr >= KS_COUNTER_CEILING)
		return 0;

	if (may_write && next_ctr + KS_LOW_WATER >= cur.limit)
		extend(); /* failure is fine while the old window lasts */

	if (next_ctr >= cur.limit)
		return 0;

	*out = next_ctr++;
	return 1;
}

uint16_t keystore_exposures(void)
{
	return have_cur ? cur.exposure : 0;
}

int keystore_bump_exposure(void)
{
	if (!have_cur)
		return 0;

	ks_record_t r = cur;

	if (r.exposure != 0xFFFF)
		r.exposure++;
	return append(&r);
}

int keystore_rekey(const uint8_t key[16])
{
	if (!have_cur)
		return 0;

	ks_record_t r = cur;

	memcpy(r.key, key, 16);
	r.exposure = 0;
	return append(&r);
}

uint32_t keystore_counter_peek(void)  { return next_ctr; }
uint32_t keystore_counter_limit(void) { return have_cur ? cur.limit : 0; }
