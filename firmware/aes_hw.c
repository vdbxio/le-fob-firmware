/*
 * AES-128 block encryption on the CH592's AES peripheral.
 *
 * WCH does not document this block. The register layout and the start /
 * busy bit come from ch32fun's examples_ch5xx/aes, which reverse
 * engineered WCH's BLE library (AES_DevAESEnc). See aes.h for why the
 * result is standard AES.
 */

#include "ch32fun.h"
#include "aes.h"
#include <string.h>

#define AES_BASE 0x4000C300u

typedef struct {
	volatile uint32_t CFG;      /* bit 0 start/busy, bit 1 decrypt */
	volatile uint32_t STA;
	volatile uint32_t rsvd[4];
	volatile uint32_t DATA[4];  /* in, then out in place */
	volatile uint32_t KEY[4];
} aes_regs_t;

#define AES_REGS ((aes_regs_t *)AES_BASE)

/* Bound on the busy wait. A block takes well under a microsecond; this is
 * only here so a wedged peripheral cannot hang the fob. */
#define AES_SPIN 10000u

void aes128_encrypt(const uint8_t key[16], const uint8_t in[16],
                    uint8_t out[16])
{
	uint32_t k[4], d[4];
	uint32_t n = AES_SPIN;

	/* Callers hand byte buffers with no alignment promise. */
	memcpy(k, key, 16);
	memcpy(d, in, 16);

	AES_REGS->CFG = 0x100; /* what WCH's library writes first */
	AES_REGS->CFG = 0;     /* encrypt */
	for (int i = 0; i < 4; i++) {
		AES_REGS->KEY[i]  = k[i];
		AES_REGS->DATA[i] = d[i];
	}
	AES_REGS->CFG |= 1;
	while ((AES_REGS->CFG & 1) && --n)
		;

	for (int i = 0; i < 4; i++)
		d[i] = AES_REGS->DATA[i];
	memcpy(out, d, 16);

	/* The key registers are readable; leave nothing of it behind. */
	for (int i = 0; i < 4; i++)
		AES_REGS->KEY[i] = 0;
}
