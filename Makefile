# Convenience wrapper; the real build lives in firmware/.
.PHONY: all fresh flash disable-reset disable-debug console clean

all fresh flash disable-reset disable-debug console clean:
	$(MAKE) -C firmware $@
