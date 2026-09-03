# Convenience wrapper; the real build lives in firmware/.
.PHONY: all flash disable-reset disable-debug console clean

all flash disable-reset disable-debug console clean:
	$(MAKE) -C firmware $@
