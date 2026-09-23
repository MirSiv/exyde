#ifndef EXYDE_BOOTINFO_H
#define EXYDE_BOOTINFO_H

#include <exyde/types.h>

/* Parse bootloader-provided info at `raw_addr` and populate the memory
 * map.  The exact format is architecture-specific; on x86_64 this is
 * Multiboot2.  Must be called before pmm_init(). */
void bootinfo_init(u64 raw_addr);

#endif /* EXYDE_BOOTINFO_H */
