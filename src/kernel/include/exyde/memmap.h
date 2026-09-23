#ifndef EXYDE_MEMMAP_H
#define EXYDE_MEMMAP_H

#include <exyde/types.h>

/* Region types — match Multiboot2 mmap entry types 1..5. */
#define MEMORY_USABLE             1u
#define MEMORY_RESERVED           2u
#define MEMORY_ACPI_RECLAIMABLE   3u
#define MEMORY_ACPI_NVS           4u
#define MEMORY_BAD                5u

typedef struct {
    paddr_t base;
    u64     size;
    u32     type;
    u32     _reserved;
} memory_region_t;

#define MEMMAP_MAX_REGIONS 128

void memmap_reset(void);
bool memmap_add(paddr_t base, u64 size, u32 type);
bool memmap_reserve(paddr_t base, u64 size);

size_t memmap_count(void);
const memory_region_t *memmap_get(size_t index);

/* Highest physical address covered by any region (base + size); 0 if empty. */
paddr_t memmap_max_paddr(void);

#endif /* EXYDE_MEMMAP_H */
