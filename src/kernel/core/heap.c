#include <exyde/heap.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
#include <exyde/panic.h>

#define HEAP_ALIGN        16u
#define HEAP_MIN_PAYLOAD  16u

/* Block header.  Layout is sized so that:
 *   - sizeof(heap_block_t) is a multiple of HEAP_ALIGN (16);
 *   - payload begins at header + 32, keeping it 16-byte aligned when
 *     the heap base is page-aligned. */
typedef struct heap_block {
    size_t             size;       /* payload bytes, header excluded */
    struct heap_block *next_free;  /* free-list link, valid only when free */
    u32                flags;
    u32                _pad;
    u64                _pad2;      /* forces sizeof to 32 */
} heap_block_t;

#define HEAP_HEADER    ((size_t)sizeof(heap_block_t))
#define BLK_FREE       0x1u

_Static_assert(sizeof(heap_block_t) == 32, "heap_block_t must be 32 bytes");
_Static_assert(HEAP_HEADER % HEAP_ALIGN == 0,
               "header must preserve payload alignment");

static vaddr_t        heap_start;
static vaddr_t        heap_end;
static heap_block_t  *heap_free_head;
static size_t         heap_live_payload;   /* sum of payload in used blocks */

static inline bool block_is_free(const heap_block_t *b) {
    return (b->flags & BLK_FREE) != 0;
}

static inline vaddr_t block_end(const heap_block_t *b) {
    return (vaddr_t)b + HEAP_HEADER + b->size;
}

/* Insert `blk` into the address-sorted free list, coalescing with
 * physically adjacent neighbours. */
static void free_list_insert(heap_block_t *blk) {
    blk->flags    |= BLK_FREE;
    blk->next_free = (heap_block_t *)0;

    heap_block_t *prev = (heap_block_t *)0;
    heap_block_t *cur  = heap_free_head;
    while (cur && (vaddr_t)cur < (vaddr_t)blk) {
        prev = cur;
        cur  = cur->next_free;
    }

    blk->next_free = cur;
    if (prev) prev->next_free = blk;
    else      heap_free_head  = blk;

    /* Merge with successor. */
    if (cur && block_end(blk) == (vaddr_t)cur) {
        blk->size     += HEAP_HEADER + cur->size;
        blk->next_free = cur->next_free;
    }

    /* Merge with predecessor. */
    if (prev && block_end(prev) == (vaddr_t)blk) {
        prev->size     += HEAP_HEADER + blk->size;
        prev->next_free = blk->next_free;
    }
}

/* First-fit within the free list.  Split if the remainder is worth it. */
static void *alloc_from_list(size_t need) {
    heap_block_t *prev = (heap_block_t *)0;
    heap_block_t *cur  = heap_free_head;

    while (cur) {
        if (cur->size >= need) {
            size_t remain = cur->size - need;
            if (remain >= HEAP_HEADER + HEAP_MIN_PAYLOAD) {
                heap_block_t *rem = (heap_block_t *)
                    ((u8 *)cur + HEAP_HEADER + need);
                rem->size      = remain - HEAP_HEADER;
                rem->flags     = BLK_FREE;
                rem->next_free = cur->next_free;

                cur->size = need;
                if (prev) prev->next_free = rem;
                else      heap_free_head  = rem;
            } else {
                if (prev) prev->next_free = cur->next_free;
                else      heap_free_head  = cur->next_free;
            }

            cur->flags    &= ~BLK_FREE;
            cur->next_free = (heap_block_t *)0;
            heap_live_payload += cur->size;
            return (u8 *)cur + HEAP_HEADER;
        }
        prev = cur;
        cur  = cur->next_free;
    }
    return (void *)0;
}

/* Map new pages at heap_end and add them as a fresh free block. */
static bool heap_grow(size_t need_payload) {
    size_t need_bytes = need_payload + HEAP_HEADER;
    size_t pages      = (need_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;

    vaddr_t base   = heap_end;
    size_t  mapped = 0;

    for (size_t i = 0; i < pages; ++i) {
        vaddr_t va = base + (vaddr_t)i * PAGE_SIZE;
        if (va + PAGE_SIZE > KERNEL_HEAP_MAX) break;

        paddr_t pa = pmm_alloc_page();
        if (pa == 0) break;

        if (!vmm_map(vmm_kernel_space(), va, pa, VM_PRESENT | VM_WRITE)) {
            pmm_free_page(pa);
            break;
        }
        mapped++;
    }
    if (mapped == 0) return false;

    size_t new_bytes = mapped * PAGE_SIZE;
    heap_end = base + new_bytes;

    heap_block_t *blk = (heap_block_t *)base;
    blk->size      = new_bytes - HEAP_HEADER;
    blk->flags     = 0;
    blk->next_free = (heap_block_t *)0;

    free_list_insert(blk);
    return true;
}

void heap_init(void) {
    heap_start          = KERNEL_HEAP_BASE;
    heap_end            = KERNEL_HEAP_BASE;
    heap_free_head      = (heap_block_t *)0;
    heap_live_payload   = 0;
}

static inline size_t round_up(size_t n) {
    return (n + HEAP_ALIGN - 1) & ~((size_t)HEAP_ALIGN - 1);
}

void *exy_malloc(size_t size) {
    if (size == 0) return (void *)0;

    size_t need = round_up(size);
    if (need < HEAP_MIN_PAYLOAD) need = HEAP_MIN_PAYLOAD;

    void *p = alloc_from_list(need);
    if (p) return p;

    if (!heap_grow(need)) return (void *)0;
    return alloc_from_list(need);
}

void *exy_zalloc(size_t size) {
    void *p = exy_malloc(size);
    if (!p) return (void *)0;
    u8 *b = (u8 *)p;
    for (size_t i = 0; i < size; ++i) b[i] = 0;
    return p;
}

void exy_free(void *ptr) {
    if (!ptr) return;

    vaddr_t a = (vaddr_t)ptr;
    if (a < heap_start || a >= heap_end) {
        panic("exy_free: pointer outside heap");
    }

    heap_block_t *blk = (heap_block_t *)(a - HEAP_HEADER);
    if (block_is_free(blk)) {
        panic("exy_free: double free");
    }

    heap_live_payload -= blk->size;
    free_list_insert(blk);
}

void *exy_realloc(void *ptr, size_t new_size) {
    if (!ptr) return exy_malloc(new_size);
    if (new_size == 0) { exy_free(ptr); return (void *)0; }

    vaddr_t a = (vaddr_t)ptr;
    if (a < heap_start || a >= heap_end) {
        panic("exy_realloc: pointer outside heap");
    }

    heap_block_t *blk = (heap_block_t *)(a - HEAP_HEADER);
    if (block_is_free(blk)) {
        panic("exy_realloc: pointer already freed");
    }

    size_t old_size = blk->size;
    size_t need     = round_up(new_size);
    if (need < HEAP_MIN_PAYLOAD) need = HEAP_MIN_PAYLOAD;

    /* Shrink is a no-op.  Keeping the block avoids needless copying. */
    if (need <= old_size) return ptr;

    void *np = exy_malloc(new_size);
    if (!np) return (void *)0;

    u8       *dst = (u8 *)np;
    const u8 *src = (const u8 *)ptr;
    for (size_t i = 0; i < old_size; ++i) dst[i] = src[i];

    exy_free(ptr);
    return np;
}

size_t heap_used_bytes(void) {
    return heap_live_payload;
}

size_t heap_free_bytes(void) {
    size_t sum = 0;
    for (heap_block_t *b = heap_free_head; b; b = b->next_free) {
        sum += b->size;
    }
    return sum;
}

size_t heap_total_bytes(void) {
    return (size_t)(heap_end - heap_start);
}
