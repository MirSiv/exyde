USER_DIR := $(CURDIR)
ROOT_DIR := $(abspath $(CURDIR)/../..)
BUILD    := $(ROOT_DIR)/build/user
OUT_ELF  := $(ROOT_DIR)/build/init.elf

CC       := gcc
AS       := nasm
LD       := ld
GCC_INC  := $(shell $(CC) -print-file-name=include)

CFLAGS := \
    -std=c17 -ffreestanding -nostdinc \
    -isystem $(GCC_INC) \
    -I$(USER_DIR)/include \
    -O2 -g \
    -Wall -Wextra -Werror \
    -fno-stack-protector -fno-builtin -fno-pic -fno-pie \
    -fno-asynchronous-unwind-tables \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 -mno-3dnow -mno-avx \
    -m64 -mcmodel=large

LDFLAGS := \
    -T $(USER_DIR)/linker/exyde.ld \
    -z max-page-size=0x1000 \
    -z noexecstack

ASFLAGS  := -f elf64 -g -F dwarf

C_SRCS := \
    libc/errno.c \
    libc/string.c \
    libc/stdlib.c \
    libc/env.c \
    libc/unistd.c \
    libc/stdio.c \
    libc/micro.c \
    init/init.c

ASM_SRCS := crt/crt0.asm

C_OBJS   := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

.PHONY: all clean

all: $(OUT_ELF)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(OUT_ELF): $(OBJS) $(USER_DIR)/linker/exyde.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

clean:
	rm -rf $(BUILD) $(OUT_ELF)
