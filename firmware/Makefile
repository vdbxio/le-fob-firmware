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
	status.c \
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

# ---------------------------------------------------------------------------
# Build knobs. Use these, not EXTRA_CFLAGS on the command line.
#
#   make CHARGER=0 flash        charger driver out
#   make CHARGER_INT=0 flash    charger driver on, but no PA4 wake/chirp
#   make CHIRP_TEXT=1 flash     chirp carries the text object (unproven)
#   make SLEEP=0 flash          stay awake (bring-up)
#   make RADIO=0 flash          no BLE (LED diagnostics)
#   make PULLUPS=0 flash        no internal pull-ups on unused pins
#   make CHIRP_MS=300000 flash  chirp every 5 min (0 = never)
#   make SETTLE_US=1000 flash   longer clock settle after wake
#
# Two things went wrong with the EXTRA_CFLAGS way and this replaces it:
#
#   A command-line EXTRA_CFLAGS *overrides* every += above, so the caller
#   had to retype the include paths or the build broke.
#
#   Worse: `make EXTRA_CFLAGS=... && make flash` builds with the flags and
#   then flashes without them. The flash target regenerates the linker
#   script, which relinks the .elf, and the relink uses whatever flags that
#   second invocation had -- the defaults. The board silently got the wrong
#   build. Knobs passed to a single `make ... flash` apply to both the build
#   and the flash, because they are the same invocation.
# ---------------------------------------------------------------------------
ifdef CHARGER
EXTRA_CFLAGS += -DFOB_ENABLE_CHARGER=$(CHARGER)
endif
ifdef CHARGER_INT
EXTRA_CFLAGS += -DFOB_ENABLE_CHARGER_INT=$(CHARGER_INT)
endif
ifdef CHIRP_TEXT
EXTRA_CFLAGS += -DFOB_CHIRP_TEXT=$(CHIRP_TEXT)
endif
ifdef SLEEP
EXTRA_CFLAGS += -DFOB_SLEEP_ENABLE=$(SLEEP)
endif
ifdef RADIO
EXTRA_CFLAGS += -DFOB_DISABLE_RADIO=$(if $(filter 0,$(RADIO)),1,0)
endif
ifdef PULLUPS
EXTRA_CFLAGS += -DFOB_QUIESCE_PULLUPS=$(PULLUPS)
endif
ifdef CHIRP_MS
EXTRA_CFLAGS += -DCHIRP_INTERVAL_MS=$(CHIRP_MS)
endif
ifdef SETTLE_US
EXTRA_CFLAGS += -DFOB_WAKE_SETTLE_US=$(SETTLE_US)
endif

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
