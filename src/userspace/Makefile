USER_DIR := $(CURDIR)
ROOT_DIR := $(abspath $(CURDIR)/../..)
BUILD    := $(ROOT_DIR)/build/user
OUT_INIT := $(ROOT_DIR)/build/init.elf
OUT_TEST := $(ROOT_DIR)/build/test.elf
OUT_ECHO := $(ROOT_DIR)/build/echo.elf

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

# libc is shared by every userspace binary.  Each binary adds its own
# main-module object (init/, test/, echo/).
LIBC_SRCS := \
    libc/errno.c \
    libc/string.c \
    libc/stdlib.c \
    libc/env.c \
    libc/unistd.c \
    libc/stdio.c \
    libc/micro.c

LIBC_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LIBC_SRCS))
CRT_OBJ   := $(BUILD)/crt/crt0.o

INIT_OBJ  := $(BUILD)/init/init.o
TEST_OBJ  := $(BUILD)/test/test.o
ECHO_OBJ  := $(BUILD)/echo/echo.o

.PHONY: all clean

all: $(OUT_INIT) $(OUT_TEST) $(OUT_ECHO)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(OUT_INIT): $(CRT_OBJ) $(LIBC_OBJS) $(INIT_OBJ) $(USER_DIR)/linker/exyde.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $(CRT_OBJ) $(LIBC_OBJS) $(INIT_OBJ)

$(OUT_TEST): $(CRT_OBJ) $(LIBC_OBJS) $(TEST_OBJ) $(USER_DIR)/linker/exyde.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $(CRT_OBJ) $(LIBC_OBJS) $(TEST_OBJ)

$(OUT_ECHO): $(CRT_OBJ) $(LIBC_OBJS) $(ECHO_OBJ) $(USER_DIR)/linker/exyde.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $(CRT_OBJ) $(LIBC_OBJS) $(ECHO_OBJ)

clean:
	rm -rf $(BUILD) $(OUT_INIT) $(OUT_TEST) $(OUT_ECHO)
