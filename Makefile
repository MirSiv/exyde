# Exyde top-level Makefile.
# Real work is delegated to subsystem Makefiles.
#
# Typical flows:
#   make                # build userspace, kernel, ISO
#   make run            # build everything and boot QEMU
#   make clean          # remove build/
#   make clean && make  # full rebuild from scratch, one command

.PHONY: all user userspace kernel iso run clean

all: iso

user userspace:
	$(MAKE) -C src/userspace

kernel: user
	$(MAKE) -C src/kernel

iso: user
	$(MAKE) -C src/kernel iso

run: iso
	$(MAKE) -C src/kernel run

clean:
	rm -rf $(CURDIR)/build
