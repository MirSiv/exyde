#include <arch/x86_64/pic.h>
#include <arch/x86_64/io.h>

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define ICW1_INIT  0x10
#define ICW1_ICW4  0x01
#define ICW4_8086  0x01

void pic_remap(u8 offset_master, u8 offset_slave) {
    u8 mask1 = inb(PIC1_DATA);
    u8 mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC1_DATA, offset_master);        io_wait();
    outb(PIC2_DATA, offset_slave);         io_wait();
    outb(PIC1_DATA, 0x04);                 io_wait(); /* slave on IRQ2 */
    outb(PIC2_DATA, 0x02);                 io_wait(); /* cascade identity */
    outb(PIC1_DATA, ICW4_8086);            io_wait();
    outb(PIC2_DATA, ICW4_8086);            io_wait();

    /* Restore saved masks.  Callers usually mask_all() right after. */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_mask_all(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(u8 irq) {
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 bit   = (u8)(irq & 0x7u);
    u8 mask  = inb(port);
    mask = (u8)(mask & ~(1u << bit));
    outb(port, mask);
}

void pic_eoi(u8 irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}
