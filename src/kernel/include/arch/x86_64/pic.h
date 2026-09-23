#ifndef EXYDE_ARCH_X86_64_PIC_H
#define EXYDE_ARCH_X86_64_PIC_H

#include <exyde/types.h>

void pic_remap(u8 offset_master, u8 offset_slave);
void pic_mask_all(void);
void pic_unmask(u8 irq);
void pic_eoi(u8 irq);

#endif /* EXYDE_ARCH_X86_64_PIC_H */
