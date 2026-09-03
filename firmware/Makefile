TARGET      := lefob
TARGET_MCU  := CH592

ADDITIONAL_C_FILES := \
	buttons.c \
	ws2812.c \
	led_anim.c \
	bthome.c \
	ble_adv.c \
	power.c \
	libc_shim.c

CH32FUN := ../ch32fun/ch32fun
MINICHLINK := ../ch32fun/minichlink

# Homebrew's riscv64-elf-gcc has no C library. -ffreestanding makes GCC's
# stdint.h use its bundled stdint-gcc.h instead of chaining to a libc one,
# and include/ declares the handful of string/stdio functions that ch32fun
# already implements itself. See include/README.md.
EXTRA_CFLAGS += -ffreestanding -Iinclude

all : $(TARGET).bin

include $(CH32FUN)/ch32fun.mk

# Flashing over the ROM USB bootloader.
#
#   Hold SW1 (PB22), plug USB in, release SW1, then run this.
#
# The -D step stops PB23 acting as hardware reset, so SW3 becomes a usable
# button. minichlink implements it over the ISP transport and checks the
# current setting first, so re-running it costs nothing.
flash : $(TARGET).bin $(MINICHLINK)/minichlink
	$(MINICHLINK)/minichlink -D
	$(MINICHLINK)/minichlink -w $< flash -b

# Just the PB23 reset-disable, if you want it without reflashing.
disable-reset : $(MINICHLINK)/minichlink
	$(MINICHLINK)/minichlink -D

# Disable the debug module. NOT part of 'flash', deliberately:
#
#   - it prompts for confirmation twice, so it would wedge an automated build
#   - it is close to one-way. Afterwards the chip can only be programmed
#     through the bootloader, and printf-over-SWIO stops working for good
#
# You need this only on a factory-fresh part that refuses to program over USB
# ISP because debug is still enabled. Run it by hand, once, knowingly.
disable-debug : $(MINICHLINK)/minichlink
	$(MINICHLINK)/minichlink -n

# printf() output over SWIO. Needs the debug module, so this works only if
# 'disable-debug' has never been run on this board. See README.
console : $(MINICHLINK)/minichlink
	$(MINICHLINK)/minichlink -T

clean : cv_clean

.PHONY: all flash disable-reset disable-debug console clean
