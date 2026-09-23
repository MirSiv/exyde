#include <exyde/types.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr   idtp;

static void idt_set_gate(u8 n, u64 handler, u16 selector, u8 ist, u8 type_attr) {
    idt[n].offset_low  = (u16)(handler & 0xFFFFu);
    idt[n].selector    = selector;
    idt[n].ist         = (u8)(ist & 0x7u);
    idt[n].type_attr   = type_attr;
    idt[n].offset_mid  = (u16)((handler >> 16) & 0xFFFFu);
    idt[n].offset_high = (u32)((handler >> 32) & 0xFFFFFFFFu);
    idt[n].reserved    = 0;
}

void idt_init(void) {
    for (unsigned i = 0; i < 256; ++i) {
        idt_set_gate((u8)i, 0, 0, 0, 0);
    }

    /* Install gates for vectors 0..47.
     *   #DF  (8)  -> IST[0] -> IDT IST index 1
     *   #NMI (2)  -> IST[1] -> IDT IST index 2
     *   #MC  (18) -> IST[2] -> IDT IST index 3
     *   #BP  (3)  -> DPL=3 (int3 from ring 3)
     * type_attr = 0x8E: present, DPL=0, 64-bit interrupt gate.
     * For #BP we use 0xEE (DPL=3) so user-mode `int $3` also works. */
    for (u8 i = 0; i < 48; ++i) {
        u8 ist = 0;
        if (i == 8)  ist = 1;
        if (i == 2)  ist = 2;
        if (i == 18) ist = 3;
        u8 type_attr = (i == 3) ? 0xEEu : 0x8Eu;
        idt_set_gate(i, (u64)isr_stub_table[i], 0x08, ist, type_attr);
    }

    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base  = (u64)&idt[0];
    __asm__ volatile("lidt %0" : : "m"(idtp));
}
