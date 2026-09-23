#include <exyde/memmap.h>

static memory_region_t regions[MEMMAP_MAX_REGIONS];
static size_t region_count;

void memmap_reset(void) {
    region_count = 0;
}

bool memmap_add(paddr_t base, u64 size, u32 type) {
    if (size == 0) return false;
    if (region_count >= MEMMAP_MAX_REGIONS) return false;
    regions[region_count].base = base;
    regions[region_count].size = size;
    regions[region_count].type = type;
    regions[region_count]._reserved = 0;
    region_count++;
    return true;
}

bool memmap_reserve(paddr_t base, u64 size) {
    return memmap_add(base, size, MEMORY_RESERVED);
}

size_t memmap_count(void) {
    return region_count;
}

const memory_region_t *memmap_get(size_t index) {
    if (index >= region_count) return (const memory_region_t *)0;
    return &regions[index];
}

paddr_t memmap_max_paddr(void) {
    paddr_t max = 0;
    for (size_t i = 0; i < region_count; ++i) {
        paddr_t end = regions[i].base + regions[i].size;
        if (end > max) max = end;
    }
    return max;
}
