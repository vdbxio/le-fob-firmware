/*
 * DataFlash stand-in for the host tests: 32 KB of RAM with NOR semantics
 * (erase sets a page to 0xFF, programming can only clear bits), plus a way
 * to cut the power part-way through the next program or erase.
 */

#include <string.h>
#include "dataflash_fake.h"
#include "../../firmware/dataflash.h"

uint8_t g_df[DATAFLASH_SIZE];
int     g_df_cut_after = -1; /* bytes to land before the "power cut", -1 = never */
int     g_df_writes, g_df_erases;
int     g_df_erase_count[DATAFLASH_SIZE / DATAFLASH_PAGE_SIZE];

void df_fake_wipe(void)
{
	memset(g_df, 0xFF, sizeof(g_df));
	memset(g_df_erase_count, 0, sizeof(g_df_erase_count));
	g_df_cut_after = -1;
	g_df_writes = g_df_erases = 0;
}

void df_read(uint32_t off, uint8_t *buf, int len)
{
	memcpy(buf, g_df + off, (size_t)len);
}

int df_write(uint32_t off, const uint8_t *buf, int len)
{
	g_df_writes++;
	for (int i = 0; i < len; i++) {
		if (g_df_cut_after == 0)
			return 0; /* the lights went out; nothing more lands */
		if (g_df_cut_after > 0)
			g_df_cut_after--;
		g_df[off + i] &= buf[i];
	}
	return memcmp(g_df + off, buf, (size_t)len) == 0;
}

int df_erase_page(uint32_t off)
{
	off &= ~(DATAFLASH_PAGE_SIZE - 1);
	g_df_erases++;
	if (g_df_cut_after == 0)
		return 0;
	g_df_erase_count[off / DATAFLASH_PAGE_SIZE]++;
	memset(g_df + off, 0xFF, DATAFLASH_PAGE_SIZE);
	return 1;
}
