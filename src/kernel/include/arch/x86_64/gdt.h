#ifndef EXYDE_ARCH_X86_64_GDT_H
#define EXYDE_ARCH_X86_64_GDT_H

#include <exyde/types.h>

void gdt_init(void);

/* Write TSS.RSP0 (kernel stack used on CPL 3 -> CPL 0 transitions). */
void gdt_set_kernel_stack(u64 rsp0);

#endif /* EXYDE_ARCH_X86_64_GDT_H */
