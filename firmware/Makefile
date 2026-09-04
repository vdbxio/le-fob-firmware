TARGET      := lefob
TARGET_MCU  := CH592

ADDITIONAL_C_FILES := \
	buttons.c \
	ws2812.c \
	led_anim.c \
	bthome.c \
	ble_adv.c \
	power.c \
	charger.c \
	chirp.c \
	libc_shim.c

CH32FUN := ../ch32fun/ch32fun
MINICHLINK := ../ch32fun/minichlink

# Homebrew's riscv64-elf-gcc has no C library. -ffreestanding makes GCC's
# stdint.h use its bundled stdint-gcc.h instead of chaining to a libc one,
# and include/ declares the handful of string/stdio functions that ch32fun
# already implements itself. See include/README.md.
EXTRA_CFLAGS += -ffreestanding -Iinclude

# lib_i2c_ch5xx.h is header-only and lives with its example in the vendored
# ch32fun tree rather than in extralibs, so it needs its own include path.
EXTRA_CFLAGS += -I$(CH32FUN)/../examples_ch5xx/i2c_sensors

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

# One-time preparation for a factory-fresh board, then flash it.
#
#   Hold SW1 (PB22), plug USB, release SW1, then run this.
#
# Does everything a new CH592 needs before it will behave like the others,
# in the order it needs it:
#
#   -D  PB23 stops acting as hardware reset, so SW3 becomes a usable button
#   -n  disables the debug module. A factory-fresh part refuses to program
#       over USB ISP with it enabled. This prompts for confirmation twice
#       and is close to one-way: afterwards the chip programs only through
#       the bootloader, and printf-over-SWIO -- `make console` -- stops
#       working on this board permanently.
#
# `make flash` deliberately does neither of the destructive halves. Use this
# once per new board, then `make flash` from then on.
fresh : $(TARGET).bin $(MINICHLINK)/minichlink
	@echo
	@echo "  === make fresh: one-time preparation for a NEW board ==="
	@echo "  Disables the debug module. minichlink will ask twice."
	@echo "  After this, 'make console' will not work on this board again;"
	@echo "  SWIO printf is gone and USB CDC is the only console route."
	@echo "  Ctrl-C now if that is not what you want."
	@echo
	$(MINICHLINK)/minichlink -D
	$(MINICHLINK)/minichlink -n
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

.PHONY: all fresh flash disable-reset disable-debug console clean
