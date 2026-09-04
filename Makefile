# Convenience wrapper; the real build lives in firmware/.
#
# Build knobs (CHARGER=0, SLEEP=0, RADIO=0, PULLUPS=0, CHIRP_MS=..,
# SETTLE_US=..) are command-line variables and reach the sub-make on their
# own. See firmware/Makefile.
.PHONY: all fresh flash disable-reset disable-debug console clean

all fresh flash disable-reset disable-debug console clean:
	$(MAKE) -C firmware $@
