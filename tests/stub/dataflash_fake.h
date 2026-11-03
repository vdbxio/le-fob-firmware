#ifndef LEFOB_TEST_DATAFLASH_FAKE_H
#define LEFOB_TEST_DATAFLASH_FAKE_H

#include <stdint.h>
#include "../../firmware/dataflash.h"

extern uint8_t g_df[DATAFLASH_SIZE];
extern int     g_df_cut_after;
extern int     g_df_writes, g_df_erases;
extern int     g_df_erase_count[DATAFLASH_SIZE / DATAFLASH_PAGE_SIZE];

/* Back to a factory-blank part: every byte 0xFF, no power cut armed. */
void df_fake_wipe(void);

#endif
