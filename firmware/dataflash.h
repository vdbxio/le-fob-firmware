#ifndef LE_FOB_DATAFLASH_H
#define LE_FOB_DATAFLASH_H

#include <stdint.h>

/*
 * The CH592's 32 KB DataFlash, addressed by offset from its start
 * (0x00070000). Erases in 256-byte pages; a write can only clear bits, so a
 * region must be erased (all 0xFF) before it is written.
 *
 * Reads go through the flash controller's read command rather than a
 * pointer dereference. The MVP spec warns that the two disagree on an
 * erased region, and a journal that writes one way and validates the other
 * would look corrupt at random.
 *
 * The host tests replace dataflash.c with tests/stub/dataflash_fake.c,
 * which can also cut power part-way through a write.
 */

#define DATAFLASH_SIZE      0x8000u
#define DATAFLASH_PAGE_SIZE 256u

void df_read(uint32_t off, uint8_t *buf, int len);

/* Returns 1 if the bytes read back exactly as written. `len` is a multiple
 * of 4 and the range does not cross a page. */
int df_write(uint32_t off, const uint8_t *buf, int len);

/* Erase the 256-byte page containing `off`. Returns 1 if it reads back
 * all 0xFF. */
int df_erase_page(uint32_t off);

#endif /* LE_FOB_DATAFLASH_H */
