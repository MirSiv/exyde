#include <exyde/arch.h>
#include <exyde/console.h>
#include <exyde/irq.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>

/* arch_irqs_enable / arch_irqs_disable are defined in irq.c. */

void arch_init(void) {
    console_init();
    gdt_init();
    idt_init();
    arch_irq_init();
}

u64 arch_irqs_save_and_disable(void) {
    u64 flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

void arch_irqs_restore(u64 flags) {
    __asm__ volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}
