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
	i2c.c \
	chirp.c \
	status.c \
	battery.c \
	temp.c \
	flashlight.c \
	isp.c \
	aes_hw.c \
	ccm.c \
	dataflash.c \
	keystore.c \
	entropy.c \
	keyhold.c \
	hidtype.c \
	usb_hid.c \
	../ch32fun/extralibs/fsusb.c \
	libc_shim.c

CH32FUN := ../ch32fun/ch32fun
MINICHLINK := ../ch32fun/minichlink

# Homebrew's riscv64-elf-gcc has no C library. -ffreestanding makes GCC's
# stdint.h use its bundled stdint-gcc.h instead of chaining to a libc one,
# and include/ declares the handful of string/stdio functions that ch32fun
# already implements itself. See include/README.md.
EXTRA_CFLAGS += -ffreestanding -Iinclude

# ---------------------------------------------------------------------------
# Build knobs. Use these, not EXTRA_CFLAGS on the command line.
#
#   make CHARGER=0 flash        charger driver out
#   make CHARGER_INT=0 flash    charger driver on, but no PA4 wake/chirp
#   make CHG_MA=96 flash        charge current in mA (multiple of 8)
#   make FLASHLIGHT_PCT=30 flash   torch momentary / latch-start duty (20 ships)
#   make FLASHLIGHT_MAX_PCT=100 flash  torch cap for the latched steps (90 ships; bench only)
#   make FLASHLIGHT=0 flash     no torch
#   make FLASHLIGHT_CHANNELS=1 flash  PWM6 only (IR fitted on Q1-A)
#   make CHIRP_TEXT=1 flash     chirp carries the text object (unproven)
#   make SLEEP=0 flash          stay awake (bring-up)
#   make RADIO=0 flash          no BLE (LED diagnostics)
#   make PULLUPS=0 flash        no internal pull-ups on unused pins
#   make CHIRP_MS=300000 flash  chirp every 5 min (0 = never)
#   make SETTLE_US=1000 flash   longer clock settle after wake
#   make ENCRYPT=0 flash        plaintext BTHome, as shipped for Supercon
#   make HID_KEYPAD=0 flash     type key digits from the number row
#   make BOOT_TRACE=1 flash     pads show each key-setup stage at boot
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
ifdef FLASHLIGHT_PCT
EXTRA_CFLAGS += -DFLASHLIGHT_PCT=$(FLASHLIGHT_PCT)
endif
ifdef FLASHLIGHT_MAX_PCT
EXTRA_CFLAGS += -DFLASHLIGHT_MAX_PCT=$(FLASHLIGHT_MAX_PCT)
endif
ifdef FLASHLIGHT
EXTRA_CFLAGS += -DFOB_ENABLE_FLASHLIGHT=$(FLASHLIGHT)
endif
ifdef FLASHLIGHT_CHANNELS
EXTRA_CFLAGS += -DFLASHLIGHT_CHANNELS=$(FLASHLIGHT_CHANNELS)
endif
ifdef LED_CHARGE
EXTRA_CFLAGS += -DPAD_LED_SHOW_CHARGE=$(LED_CHARGE)
endif
ifdef CHG_MA
EXTRA_CFLAGS += -DCHARGER_ICHG_MA=$(CHG_MA)
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
ifdef ENCRYPT
EXTRA_CFLAGS += -DFOB_ENCRYPT=$(ENCRYPT)
endif
ifdef BOOT_TRACE
EXTRA_CFLAGS += -DFOB_BOOT_TRACE=$(BOOT_TRACE)
endif
ifdef HID_KEYPAD
EXTRA_CFLAGS += -DHID_KEYPAD_DIGITS=$(HID_KEYPAD)
endif
# Transmit power in dBm: one of -25 -20 -15 -10 -8 -5 -3 -1 0 1 2 3 4 5 6 7
# (the LL_TX_POWER_* table in ch5xxhw.h). `make TXPOWER=7 flash`.
ifdef TXPOWER
EXTRA_CFLAGS += -DBLE_TX_POWER=LL_TX_POWER_$(subst -,MINUS_,$(TXPOWER))_DBM \
                -DBLE_TX_POWER_DBM=$(TXPOWER)
endif
# Burst shape: how many repeats, spread evenly over how long (ms). The gap
# between repeats is derived. `make ROUNDS=12 BURST_MS=1200 flash`.
ifdef ROUNDS
EXTRA_CFLAGS += -DADV_BURST_ROUNDS=$(ROUNDS)
endif
ifdef BURST_MS
EXTRA_CFLAGS += -DADV_BURST_MS=$(BURST_MS)
endif
# 32 MHz crystal load trim in pF (even, 10..24); see board.h.
ifdef HSE_CAP
EXTRA_CFLAGS += -DFOB_HSE_CAP_PF=$(HSE_CAP)
endif
# Bootloader-entry hold length (ms), and whether it insists on VBUS.
ifdef ISP_HOLD_MS
EXTRA_CFLAGS += -DISP_HOLD_MS=$(ISP_HOLD_MS)
endif
ifdef ISP_REQUIRE_USB
EXTRA_CFLAGS += -DFOB_ISP_REQUIRE_USB=$(ISP_REQUIRE_USB)
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
