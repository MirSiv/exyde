#ifndef EXYDE_PMM_H
#define EXYDE_PMM_H

#include <exyde/types.h>

#define PAGE_SIZE   4096u
#define PAGE_SHIFT  12u

/* Implementation-time cap on the tracked physical address range.
 * It is a build-time parameter, not part of the PMM API:
 * raise it with -DPMM_MAX_TRACKED_BYTES=... and nothing else changes.
 * It currently matches the bootstrap identity map (1 GiB). */
#ifndef PMM_MAX_TRACKED_BYTES
#define PMM_MAX_TRACKED_BYTES (1ull * 1024ull * 1024ull * 1024ull)
#endif

/* Initialize the physical page allocator from the current memory map.
 * Must be called once, after bootinfo_init() and after all reserve()
 * calls.  Re-initialization is allowed and rebuilds state from memmap. */
void     pmm_init(void);

/* Allocate `count` contiguous pages (or 1 for pmm_alloc_page).
 * Returns physical base address, or 0 on failure. */
paddr_t  pmm_alloc_page(void);
paddr_t  pmm_alloc_pages(size_t count);

/* Free a page or a contiguous range.
 * Returns false if the page(s) are not currently allocated, are
 * permanently reserved, are unaligned, or are out of range.
 * Double-free is detected and rejected. */
bool     pmm_free_page(paddr_t page);
bool     pmm_free_pages(paddr_t page, size_t count);

/* O(1) query: is this page currently marked used? */
bool     pmm_is_allocated(paddr_t page);

/* Introspection. */
paddr_t  pmm_max_paddr(void);
size_t   pmm_total_page_count(void);
size_t   pmm_free_page_count(void);

#endif /* EXYDE_PMM_H */
