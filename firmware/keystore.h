#ifndef LE_FOB_KEYSTORE_H
#define LE_FOB_KEYSTORE_H

#include <stdint.h>

/*
 * The fob's persistent secrets and counters, journaled in DataFlash:
 *
 *   bindkey          16 bytes, born on the fob (entropy.c), never set
 *                    from outside
 *   replay counter   the BTHome encryption counter. Starts at
 *                    KS_COUNTER_START, only ever goes up, and survives
 *                    sleep, flat batteries, reboots and application
 *                    updates
 *   exposure count   how many times the key has been typed out over USB.
 *                    Only a rekey puts it back to zero
 *
 * How the counter stays monotonic without a flash write per advertisement
 * (MVP spec section 12): the journal never stores the counter itself, only
 * a reservation -- an upper bound, exclusive, that values in use must stay
 * below. Each boot reserves KS_RESERVE values ahead and the fob runs from
 * RAM inside that window, topping it up when it gets within KS_LOW_WATER
 * of the end. A battery yanked at any point resumes from the highest
 * reservation ever written, which is at or above anything ever sent.
 *
 * Each record is self-checking (CRC-32) and written to a fresh slot before
 * the old one is abandoned, so a write torn by a power cut leaves the
 * previous record in charge. Records rotate through KS_PAGES pages so no
 * page is erased more than once per (records per page) writes.
 */

typedef enum {
	KS_OK = 0,      /* a valid record was found */
	KS_EMPTY,       /* none: first boot, or DataFlash was erased */
} ks_state_t;

/* Scan the journal. Must run before anything else here. */
ks_state_t keystore_open(void);

/*
 * First-time provisioning: store `key` with the counter at
 * KS_COUNTER_START and the exposure count at zero. Returns 1 on a verified
 * write.
 */
int keystore_provision(const uint8_t key[16]);

/* The bindkey, or 0 if there is none. */
const uint8_t *keystore_key(void);

/*
 * Reserve a fresh counter window at boot. Returns 1 on success; on failure
 * the fob has nothing it may transmit with until a later write succeeds.
 */
int keystore_reserve(void);

/*
 * Take the next counter value for an advertisement.
 *
 * `may_write` says whether a flash write is allowed right now: never while
 * the torch is on (a 100 mAh cell sags hard under it, and that sag must not
 * land on a flash program). When the window is running low and writing is
 * allowed, it is topped up first.
 *
 * Returns 0 -- and the frame must not go out -- if the window is used up
 * and could not be extended, or if the counter is about to hit 2^32
 * (it stops rather than wrapping; a rekey is the way back).
 */
int keystore_next_counter(int may_write, uint32_t *out);

/* Exposure count as last persisted. */
uint16_t keystore_exposures(void);

/*
 * Bump the exposure count and persist it. Called *before* the first
 * character of the key is typed, so an interrupted readout still counts.
 * Returns 1 on a verified write; on 0 the readout must not happen.
 */
int keystore_bump_exposure(void);

/*
 * Replace the bindkey with `key` and zero the exposure count. The counter
 * carries on upward, so nothing sent under the old key can be replayed
 * under the new one either. Returns 1 on a verified write.
 */
int keystore_rekey(const uint8_t key[16]);

/* For the tests and the banner: the window as it stands. */
uint32_t keystore_counter_peek(void);
uint32_t keystore_counter_limit(void);

#endif /* LE_FOB_KEYSTORE_H */
