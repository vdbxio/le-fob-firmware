/*
 * DataFlash access through the flash controller. See dataflash.h.
 *
 * Built on the low-level command helpers in ch32fun's
 * extralibs/ch5xx_flash.h (begin / out / in / wait), which already live in
 * RAM: the controller cannot fetch instructions from flash while it is busy
 * running a command, so everything here that talks to it is __HIGH_CODE
 * too. The helpers turn interrupts off for the duration of a command and
 * back on when it closes.
 *
 * The library's own cmd_* layer is not used: cmd_erase() aligns to 4 KB
 * sectors whatever it is asked for, cmd_read() is a pointer read, and none
 * of them are placed in RAM.
 */

#include "ch32fun.h"
#include "ch5xx_flash.h"
#include "dataflash.h"

/*
 * Where DataFlash sits in the flash controller's command address space.
 *
 * It is mapped for pointer reads at 0x00070000, but WCH's own
 * FLASH_EEPROM_CMD (libISP592.a, disassembled) addresses it for commands
 * at 0x00070000 | 0x00080000: the controller wants bit 19 set to pick the
 * data area. Commands sent to 0x00070000 land in the code area instead.
 */
#define DATAFLASH_BASE (0x00070000u | 0x00080000u)

#define FLASH_CMD_FAST_READ    0x0B
#define FLASH_CMD_PAGE_PROGRAM 0x02
#define FLASH_CMD_PAGE_ERASE   0x81

__HIGH_CODE
void df_read(uint32_t off, uint8_t *buf, int len)
{
	ch5xx_flash_rom_open_read();
	/* For 0x0B the helper sends the address plus the two dummy bytes. */
	ch5xx_flash_rom_addr(FLASH_CMD_FAST_READ, DATAFLASH_BASE + off);
	for (int i = 0; i < len; i++)
		buf[i] = ch5xx_flash_rom_in();
	ch5xx_flash_rom_close();
}

__HIGH_CODE
static int df_matches(uint32_t off, const uint8_t *want, int len)
{
	int ok = 1;

	ch5xx_flash_rom_open_read();
	ch5xx_flash_rom_addr(FLASH_CMD_FAST_READ, DATAFLASH_BASE + off);
	for (int i = 0; i < len; i++)
		if (ch5xx_flash_rom_in() != (want ? want[i] : 0xFF))
			ok = 0;
	ch5xx_flash_rom_close();
	return ok;
}

__HIGH_CODE
int df_write(uint32_t off, const uint8_t *buf, int len)
{
	if ((len & 3) || (off & 3) ||
	    (off / DATAFLASH_PAGE_SIZE) != ((off + len - 1) / DATAFLASH_PAGE_SIZE))
		return 0;

	/*
	 * Page program the way WCH's FLASH_EEPROM_CMD does it for the data
	 * area: one byte at a time through the data register, then wait. (The
	 * library's cmd_write() uses the word-wide code-area sequence instead,
	 * which is not what the data area expects.)
	 */
	ch5xx_flash_rom_open_erase_write();
	ch5xx_flash_rom_addr(FLASH_CMD_PAGE_PROGRAM, DATAFLASH_BASE + off);
	for (int i = 0; i < len; i++)
		ch5xx_flash_rom_out(buf[i]);
	uint8_t ok = ch5xx_flash_rom_wait();
	ch5xx_flash_rom_close();

	return ok && df_matches(off, buf, len);
}

__HIGH_CODE
int df_erase_page(uint32_t off)
{
	off &= ~(DATAFLASH_PAGE_SIZE - 1);

	ch5xx_flash_rom_open_erase_write();
	ch5xx_flash_rom_addr(FLASH_CMD_PAGE_ERASE, DATAFLASH_BASE + off);
	uint8_t ok = ch5xx_flash_rom_wait();
	ch5xx_flash_rom_close();

	return ok && df_matches(off, 0, DATAFLASH_PAGE_SIZE);
}
