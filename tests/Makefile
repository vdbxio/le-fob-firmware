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

test_bthome : test_bthome.c ../firmware/bthome.c
	$(CC) $(CFLAGS) -o $@ $^

test_buttons : test_buttons.c ../firmware/buttons.c
	$(CC) $(CFLAGS) -Dinterrupt=used -o $@ $^

test_leds : test_leds.c ../firmware/led_anim.c
	$(CC) $(CFLAGS) -o $@ $^

test_power : test_power.c ../firmware/power.c
	$(CC) $(CFLAGS) -Dinterrupt=used -o $@ $^

test_charger : test_charger.c ../firmware/charger.c
	$(CC) $(CFLAGS) -o $@ $^

test_chirp : test_chirp.c ../firmware/chirp.c
	$(CC) $(CFLAGS) -o $@ $^

test_status : test_status.c ../firmware/status.c
	$(CC) $(CFLAGS) -o $@ $^

test_isp : test_isp.c ../firmware/isp.c
	$(CC) $(CFLAGS) -o $@ $^

run : test_bthome test_buttons test_leds test_power test_charger test_chirp test_status test_isp
	./test_bthome
	./test_buttons
	./test_leds
	./test_power
	./test_charger
	./test_chirp
	./test_status
	./test_isp

clean :
	rm -rf test_bthome test_buttons test_leds test_power test_charger test_chirp test_status test_isp *.dSYM
