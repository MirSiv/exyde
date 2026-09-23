#include <exyde/pmm.h>
#include <exyde/memmap.h>
#include <exyde/panic.h>

/* Bitmap size derived from the configured cap.  Only usable RAM below
 * the cap is tracked.  Reserved MMIO above the usable range is not
 * tracked at all — the allocator has no reason to touch it. */
#define PMM_BITMAP_MAX_BYTES  ((PMM_MAX_TRACKED_BYTES / PAGE_SIZE) / 8u)
#define PMM_BITS_PER_WORD     64u

static u64 used_bitmap[PMM_BITMAP_MAX_BYTES / sizeof(u64)];
static u64 resv_bitmap[PMM_BITMAP_MAX_BYTES / sizeof(u64)];

static size_t   bitmap_words;
static size_t   total_pages;
static size_t   free_pages;
static paddr_t  max_paddr;

/* ---- bit helpers ------------------------------------------------------ */

static inline void used_set(size_t page) {
    used_bitmap[page / PMM_BITS_PER_WORD] |= (u64)1 << (page % PMM_BITS_PER_WORD);
}
static inline void used_clear(size_t page) {
    used_bitmap[page / PMM_BITS_PER_WORD] &= ~((u64)1 << (page % PMM_BITS_PER_WORD));
}
static inline bool used_get(size_t page) {
    return (used_bitmap[page / PMM_BITS_PER_WORD] >> (page % PMM_BITS_PER_WORD)) & 1u;
}
static inline void resv_set(size_t page) {
    resv_bitmap[page / PMM_BITS_PER_WORD] |= (u64)1 << (page % PMM_BITS_PER_WORD);
}
static inline bool resv_get(size_t page) {
    return (resv_bitmap[page / PMM_BITS_PER_WORD] >> (page % PMM_BITS_PER_WORD)) & 1u;
}

/* ---- range marking ---------------------------------------------------- */

static void mark_usable(paddr_t base, u64 size) {
    paddr_t start = (base + PAGE_SIZE - 1) & ~((paddr_t)PAGE_SIZE - 1);
    paddr_t end   = (base + size) & ~((paddr_t)PAGE_SIZE - 1);
    if (end > max_paddr) end = max_paddr;
    if (start >= end) return;
    for (paddr_t a = start; a < end; a += PAGE_SIZE) {
        used_clear((size_t)(a >> PAGE_SHIFT));
    }
}

static void mark_reserved(paddr_t base, u64 size) {
    paddr_t start = base & ~((paddr_t)PAGE_SIZE - 1);
    paddr_t end   = (base + size + PAGE_SIZE - 1) & ~((paddr_t)PAGE_SIZE - 1);
    if (end > max_paddr) end = max_paddr;
    if (start >= end) return;
    for (paddr_t a = start; a < end; a += PAGE_SIZE) {
        size_t p = (size_t)(a >> PAGE_SHIFT);
        used_set(p);
        resv_set(p);
    }
}

/* Highest end of any USABLE region, rounded up to PAGE_SIZE. */
static paddr_t usable_top(void) {
    paddr_t top = 0;
    for (size_t i = 0; i < memmap_count(); ++i) {
        const memory_region_t *r = memmap_get(i);
        if (r->type != MEMORY_USABLE) continue;
        paddr_t e = r->base + r->size;
        if (e > top) top = e;
    }
    if (top == 0) return 0;
    return (top + PAGE_SIZE - 1) & ~((paddr_t)PAGE_SIZE - 1);
}

/* ---- public API ------------------------------------------------------- */

void pmm_init(void) {
    paddr_t top = usable_top();
    if (top == 0) {
        panic("pmm: no usable memory");
    }
    if (top > PMM_MAX_TRACKED_BYTES) {
        top = PMM_MAX_TRACKED_BYTES;
    }
    max_paddr = top;

    total_pages  = (size_t)(max_paddr >> PAGE_SHIFT);
    bitmap_words = (total_pages + PMM_BITS_PER_WORD - 1) / PMM_BITS_PER_WORD;

    if (bitmap_words * sizeof(u64) > PMM_BITMAP_MAX_BYTES) {
        panic("pmm: bitmap capacity exceeded");
    }

    /* Default: every tracked page is used and not reserved. */
    for (size_t i = 0; i < bitmap_words; ++i) {
        used_bitmap[i] = ~(u64)0;
        resv_bitmap[i] = 0;
    }

    /* Pass 1: free all USABLE regions. */
    for (size_t i = 0; i < memmap_count(); ++i) {
        const memory_region_t *r = memmap_get(i);
        if (r->type == MEMORY_USABLE) {
            mark_usable(r->base, r->size);
        }
    }

    /* Pass 2: lock every non-usable region; reserved wins on overlap. */
    for (size_t i = 0; i < memmap_count(); ++i) {
        const memory_region_t *r = memmap_get(i);
        if (r->type != MEMORY_USABLE) {
            mark_reserved(r->base, r->size);
        }
    }

    free_pages = 0;
    for (size_t i = 0; i < total_pages; ++i) {
        if (!used_get(i)) free_pages++;
    }
}

paddr_t pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

paddr_t pmm_alloc_pages(size_t count) {
    if (count == 0 || count > free_pages) return 0;

    size_t run = 0;
    for (size_t i = 0; i < total_pages; ++i) {
        if (used_get(i)) { run = 0; continue; }
        run++;
        if (run == count) {
            size_t start = i + 1 - count;
            for (size_t j = 0; j < count; ++j) {
                used_set(start + j);
            }
            free_pages -= count;
            return (paddr_t)start << PAGE_SHIFT;
        }
    }
    return 0;
}

bool pmm_free_page(paddr_t page) {
    return pmm_free_pages(page, 1);
}

bool pmm_free_pages(paddr_t page, size_t count) {
    if (count == 0) return false;
    if (page & (paddr_t)(PAGE_SIZE - 1)) return false;
    if (page >= max_paddr) return false;

    paddr_t end = page + ((paddr_t)count << PAGE_SHIFT);
    if (end > max_paddr) return false;

    size_t start = (size_t)(page >> PAGE_SHIFT);

    /* Validate first: no reserved, no already-free (double free). */
    for (size_t i = 0; i < count; ++i) {
        size_t p = start + i;
        if (resv_get(p))  return false;
        if (!used_get(p)) return false;
    }

    for (size_t i = 0; i < count; ++i) {
        used_clear(start + i);
    }
    free_pages += count;
    return true;
}

bool pmm_is_allocated(paddr_t page) {
    if (page & (paddr_t)(PAGE_SIZE - 1)) return false;
    if (page >= max_paddr) return false;
    return used_get((size_t)(page >> PAGE_SHIFT));
}

paddr_t pmm_max_paddr(void)         { return max_paddr; }
size_t  pmm_total_page_count(void)  { return total_pages; }
size_t  pmm_free_page_count(void)   { return free_pages; }
