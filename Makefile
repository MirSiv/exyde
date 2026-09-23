# Exyde top-level Makefile.
# Real work is delegated to subsystem Makefiles.

.PHONY: all kernel iso run clean

all: iso

kernel:
	$(MAKE) -C src/kernel

iso:
	$(MAKE) -C src/kernel iso

run:
	$(MAKE) -C src/kernel run

clean:
	$(MAKE) -C src/kernel clean
