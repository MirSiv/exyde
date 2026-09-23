# Exyde top-level Makefile.
# Real work is delegated to subsystem Makefiles.

.PHONY: all kernel user iso run clean

all: iso

user:
	$(MAKE) -C src/userspace

kernel: user
	$(MAKE) -C src/kernel

iso: user
	$(MAKE) -C src/kernel iso

run: iso
	$(MAKE) -C src/kernel run

clean:
	$(MAKE) -C src/userspace clean
	$(MAKE) -C src/kernel clean
