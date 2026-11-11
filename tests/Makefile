# Host-side tests. These compile the target's own bthome.c and buttons.c
# natively against a stub ch32fun.h, so the packet layout and the gesture
# state machine are checked by running them rather than by reading them.
#
# -Dinterrupt=used rewrites buttons.c's __attribute__((interrupt)) into
# something a host compiler accepts, so the ISRs need no #ifdef in the
# firmware itself.

CC ?= cc
CFLAGS := -Wall -Wextra -O1 -g -Istub \
          -DLEFOB_ROM_MAC_ADDR=g_fake_rom

.PHONY: all clean
all : run

test_bthome : test_bthome.c ../firmware/bthome.c ../firmware/ccm.c stub/aes_soft.c
	$(CC) $(CFLAGS) -o $@ $^

test_buttons : test_buttons.c ../firmware/buttons.c
	$(CC) $(CFLAGS) -Dinterrupt=used -o $@ $^

test_leds : test_leds.c ../firmware/led_anim.c
	$(CC) $(CFLAGS) -o $@ $^

test_power : test_power.c ../firmware/power.c
	$(CC) $(CFLAGS) -Dinterrupt=used -o $@ $^

test_charger : test_charger.c ../firmware/charger.c stub/i2c_fake.c
	$(CC) $(CFLAGS) -o $@ $^

test_chirp : test_chirp.c ../firmware/chirp.c
	$(CC) $(CFLAGS) -o $@ $^

test_battery : test_battery.c ../firmware/battery.c
	$(CC) $(CFLAGS) -o $@ $^

test_flashlight : test_flashlight.c ../firmware/flashlight.c
	$(CC) $(CFLAGS) -o $@ $^

test_temp : test_temp.c ../firmware/temp.c
	$(CC) $(CFLAGS) -DLEFOB_ROM_TMP25C_ADDR='&g_fake_tmp25c' -o $@ $^

test_status : test_status.c ../firmware/status.c
	$(CC) $(CFLAGS) -o $@ $^

test_isp : test_isp.c ../firmware/isp.c
	$(CC) $(CFLAGS) -o $@ $^

test_ccm : test_ccm.c ../firmware/ccm.c stub/aes_soft.c
	$(CC) $(CFLAGS) -o $@ $^

test_keystore : test_keystore.c ../firmware/keystore.c stub/dataflash_fake.c
	$(CC) $(CFLAGS) -o $@ $^

test_keyhold : test_keyhold.c ../firmware/keyhold.c ../firmware/hidtype.c
	$(CC) $(CFLAGS) -o $@ $^

test_entropy : test_entropy.c ../firmware/entropy.c ../firmware/ccm.c stub/aes_soft.c
	$(CC) $(CFLAGS) -DLEFOB_HOST_TEST -o $@ $^

run : test_bthome test_buttons test_leds test_power test_charger test_chirp test_status test_isp test_battery test_temp test_flashlight test_ccm test_keystore test_entropy test_keyhold
	./test_bthome
	./test_buttons
	./test_leds
	./test_power
	./test_charger
	./test_chirp
	./test_status
	./test_isp
	./test_battery
	./test_temp
	./test_flashlight
	./test_ccm
	./test_keystore
	./test_entropy
	./test_keyhold

clean :
	rm -rf test_bthome test_buttons test_leds test_power test_charger test_chirp test_status test_isp test_battery test_temp test_flashlight test_ccm test_keystore test_entropy test_keyhold *.dSYM
