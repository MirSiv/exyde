KERNEL_DIR := $(CURDIR)
ROOT_DIR   := $(abspath $(CURDIR)/../..)
BUILD      := $(ROOT_DIR)/build
OBJDIR     := $(BUILD)/obj
KERNEL     := $(BUILD)/exyde-kernel.elf
ISO        := $(BUILD)/exyde.iso
ISODIR     := $(BUILD)/iso
INIT_ELF   := $(BUILD)/init.elf
TEST_ELF   := $(BUILD)/test.elf
ECHO_ELF   := $(BUILD)/echo.elf
VFS_ELF    := $(BUILD)/exy-vfs.elf
CONS_ELF   := $(BUILD)/console.elf
INIT_OBJ   := $(OBJDIR)/init_elf.o
TEST_OBJ   := $(OBJDIR)/test_elf.o
ECHO_OBJ   := $(OBJDIR)/echo_elf.o
VFS_OBJ    := $(OBJDIR)/vfs_elf.o
CONS_OBJ   := $(OBJDIR)/console_elf.o

CC      := gcc
AS      := nasm
LD      := ld
OBJCOPY := objcopy

CFLAGS := \
    -std=c17 -ffreestanding -O2 -g \
    -Wall -Wextra -Werror \
    -fno-stack-protector -fno-stack-check -fno-lto \
    -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 -mno-3dnow -mno-avx \
    -m64 \
    -I$(KERNEL_DIR)/include

ASFLAGS := -f elf64 -g -F dwarf

LDFLAGS := \
    -T $(KERNEL_DIR)/linker/x86_64.ld \
    -nostdlib -static \
    -z max-page-size=0x1000 \
    -z noexecstack

C_SRCS := \
    core/kmain.c \
    core/panic.c \
    core/irq.c \
    core/memmap.c \
    core/pmm.c \
    core/heap.c \
    core/thread.c \
    core/sched.c \
    core/mutex.c \
    core/semaphore.c \
    core/condvar.c \
    core/channel.c \
    core/elf.c \
    core/elf_table.c \
    core/process.c \
    core/handle.c \
    core/syscall.c \
    core/vfs.c \
    core/ramfs.c \
    core/fd.c \
    core/condev.c \
    core/exec.c \
    core/uaccess.c \
    core/extable.c \
    arch/x86_64/boot/multiboot2.c \
    arch/x86_64/cpu/arch.c \
    arch/x86_64/cpu/console.c \
    arch/x86_64/cpu/serial.c \
    arch/x86_64/cpu/gdt.c \
    arch/x86_64/cpu/idt.c \
    arch/x86_64/cpu/isr.c \
    arch/x86_64/cpu/pic.c \
    arch/x86_64/cpu/irq.c \
    arch/x86_64/cpu/pit.c \
    arch/x86_64/cpu/syscall_arch.c \
    arch/x86_64/mm/vmm.c

ASM_SRCS := \
    arch/x86_64/boot/boot.asm \
    arch/x86_64/cpu/gdt_flush.asm \
    arch/x86_64/cpu/isr_stubs.asm \
    arch/x86_64/cpu/switch.asm \
    arch/x86_64/cpu/enter_user.asm \
    arch/x86_64/cpu/syscall.asm \
    arch/x86_64/cpu/uaccess_asm.asm

C_OBJS   := $(patsubst %.c,$(OBJDIR)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst %.asm,$(OBJDIR)/%.o,$(ASM_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS) $(INIT_OBJ) $(TEST_OBJ) $(ECHO_OBJ) $(VFS_OBJ) $(CONS_OBJ)

.PHONY: all iso run clean

all: $(KERNEL)
iso: $(ISO)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

# Embed each userspace ELF as a byte array.  objcopy mangles the
# relative path into stable symbol names, so we always invoke it from
# the project root:
#     build/init.elf  ->  _binary_build_init_elf_{start,end}
#     build/test.elf  ->  _binary_build_test_elf_{start,end}
#     build/echo.elf  ->  _binary_build_echo_elf_{start,end}
$(INIT_OBJ): $(INIT_ELF)
	@mkdir -p $(@D)
	cd $(ROOT_DIR) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    build/init.elf build/obj/init_elf.o

$(TEST_OBJ): $(TEST_ELF)
	@mkdir -p $(@D)
	cd $(ROOT_DIR) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    build/test.elf build/obj/test_elf.o

$(ECHO_OBJ): $(ECHO_ELF)
	@mkdir -p $(@D)
	cd $(ROOT_DIR) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    build/echo.elf build/obj/echo_elf.o

$(VFS_OBJ): $(VFS_ELF)
	@mkdir -p $(@D)
	cd $(ROOT_DIR) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    build/exy-vfs.elf build/obj/vfs_elf.o

$(CONS_OBJ): $(CONS_ELF)
	@mkdir -p $(@D)
	cd $(ROOT_DIR) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    build/console.elf build/obj/console_elf.o

# Fallback: build userspace if any of the ELFs is missing.
$(INIT_ELF) $(TEST_ELF) $(ECHO_ELF) $(VFS_ELF) $(CONS_ELF):
	$(MAKE) -C $(ROOT_DIR)/src/userspace

$(KERNEL): $(OBJS) $(KERNEL_DIR)/linker/x86_64.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(ISO): $(KERNEL)
	@mkdir -p $(ISODIR)/boot/grub
	@cp $(KERNEL) $(ISODIR)/boot/exyde-kernel.elf
	@printf 'set timeout=0\nset default=0\nmenuentry "Exyde" {\n    multiboot2 /boot/exyde-kernel.elf\n    boot\n}\n' > $(ISODIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISODIR)

run: $(ISO)
	qemu-system-x86_64 -accel kvm -cdrom $(ISO) -serial stdio -m 512 -no-reboot

clean:
	rm -rf $(BUILD)
