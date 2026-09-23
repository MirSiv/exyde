KERNEL_DIR := $(CURDIR)
ROOT_DIR   := $(abspath $(CURDIR)/../..)
BUILD      := $(ROOT_DIR)/build
OBJDIR     := $(BUILD)/obj
KERNEL     := $(BUILD)/exyde-kernel.elf
ISO        := $(BUILD)/exyde.iso
ISODIR     := $(BUILD)/iso

CC := gcc
AS := nasm
LD := ld

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
    core/process.c \
    core/handle.c \
    core/syscall.c \
    core/vfs.c \
    core/ramfs.c \
    core/fd.c \
    core/condev.c \
    core/exec.c \
    core/init_elf.c \
    core/uaccess.c \
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
    arch/x86_64/cpu/syscall.asm

C_OBJS   := $(patsubst %.c,$(OBJDIR)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst %.asm,$(OBJDIR)/%.o,$(ASM_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

.PHONY: all iso run clean

all: $(KERNEL)
iso: $(ISO)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

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
