#include <exyde/irq.h>
#include <exyde/arch.h>
#include <arch/x86_64/pic.h>

/* We remap the 8259 PICs so that:
 *   master IRQ0..IRQ7  -> vectors 0x20..0x27 (32..39)
 *   slave  IRQ8..IRQ15 -> vectors 0x28..0x2F (40..47)
 *
 * This keeps them clear of the CPU exception vectors (0..31).
 */
#define PIC_MASTER_OFFSET 0x20u
#define PIC_SLAVE_OFFSET  0x28u

void arch_irq_init(void) {
    pic_remap(PIC_MASTER_OFFSET, PIC_SLAVE_OFFSET);
    pic_mask_all();

    /* Only the PIT (IRQ0) is unmasked for Subphase 0.4.
     * Keyboard, cascade, and the rest stay masked until their
     * drivers exist. */
    pic_unmask(0);
}

void arch_irq_eoi(u8 irq) {
    pic_eoi(irq);
}

void arch_irqs_enable(void) {
    __asm__ volatile("sti");
}

void arch_irqs_disable(void) {
    __asm__ volatile("cli");
}
