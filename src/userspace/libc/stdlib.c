#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

/*
 * malloc / free / calloc / realloc -- Phase 11.1.3.
 *
 * Boundary-tag allocator over brk.  The arena is one contiguous region
 * grown by sbrk() in 64 KiB chunks.  Every block is preceded by a
 * 16-byte header:
 *
 *     struct block { size_t size; size_t prev_size; };
 *
 *   - `size`      : payload size in bytes; low bit is the FREE flag.
 *   - `prev_size` : payload size of the preceding block, or 0 if this
 *                   is the first block on the arena.
 *
 * free() never returns pages to the kernel -- that would require sbrk
 * to shrink and a per-page tracking map.  It coalesces with adjacent
 * free blocks, so a long-lived process keeps reusing the same region.
 *
 * malloc() picks the first free block that fits and splits it when the
 * remainder can hold its own header plus MIN_PAYLOAD bytes.  No explicit
 * free list is kept -- walking the arena is O(blocks), which is fine
 * for an early userspace.  A segregated free list can come later.
 *
 * realloc() grows in place when the following block is free and large
 * enough; otherwise it falls back to malloc + memcpy + free.
 *
 * The allocator is single-threaded.  When userspace threads arrive
 * (Phase 12+), add a lock or per-thread arenas.
 */

#define ARENA_CHUNK  ((size_t)65536)
#define ALIGNMENT    ((size_t)16)
#define HDR_SIZE     ((size_t)(2 * sizeof(size_t)))   /* 16 */
#define MIN_PAYLOAD  ((size_t)ALIGNMENT)
#define FREE_BIT     ((size_t)1)

typedef struct block {
    size_t size;
    size_t prev_size;
} block_t;

static unsigned char *arena_hi    = NULL;  /* one past last managed byte */
static block_t       *arena_first = NULL;  /* lowest block on the arena  */
static block_t       *arena_last  = NULL;  /* highest block on the arena */

static inline size_t blk_size(const block_t *b) { return b->size & ~FREE_BIT; }
static inline int    blk_free(const block_t *b) { return (b->size & FREE_BIT) != 0; }
static inline void   blk_set_size(block_t *b, size_t sz, int free) {
    b->size = (sz & ~FREE_BIT) | (free ? FREE_BIT : 0);
}
static inline void   blk_set_free(block_t *b, int free) {
    b->size = (b->size & ~FREE_BIT) | (free ? FREE_BIT : 0);
}

static inline size_t align_up(size_t n) {
    return (n + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}

static block_t *blk_next(block_t *b) {
    unsigned char *p = (unsigned char *)b + HDR_SIZE + blk_size(b);
    if (p >= arena_hi) return NULL;
    return (block_t *)p;
}

static block_t *blk_prev(block_t *b) {
    if (b->prev_size == 0) return NULL;
    unsigned char *p = (unsigned char *)b - HDR_SIZE - b->prev_size;
    if (p < (unsigned char *)arena_first) return NULL;
    return (block_t *)p;
}

/* Trim `b` down to `need` payload bytes, turning the tail into a new
 * free block.  No-op when the remainder would be too small to hold a
 * header plus MIN_PAYLOAD.  Marks `b` as allocated on success. */
static void split_block(block_t *b, size_t need) {
    size_t cur = blk_size(b);
    if (cur < need + HDR_SIZE + MIN_PAYLOAD) {
        blk_set_free(b, 0);
        return;
    }
    unsigned char *tail = (unsigned char *)b + HDR_SIZE + need;
    block_t *nb = (block_t *)tail;
    blk_set_size(nb, cur - need - HDR_SIZE, 1);
    nb->prev_size = need;
    blk_set_size(b, need, 0);

    block_t *after = blk_next(nb);
    if (after) after->prev_size = blk_size(nb);
    else       arena_last = nb;
}

/* Append a new free block at `start` occupying `want` bytes including
 * its header.  `start` must equal arena_hi (or the initial arena start
 * when the arena is empty). */
static void arena_append(unsigned char *start, size_t want) {
    block_t *nb = (block_t *)start;
    blk_set_size(nb, want - HDR_SIZE, 1);
    nb->prev_size = (arena_last ? blk_size(arena_last) : 0);

    if (!arena_first) arena_first = nb;
    arena_last = nb;
    arena_hi   = start + want;

    /* Merge with the previous tail if it happens to be free. */
    block_t *pb = blk_prev(nb);
    if (pb && blk_free(pb)) {
        blk_set_size(pb, blk_size(pb) + HDR_SIZE + blk_size(nb), 1);
        arena_last = pb;
    }
}

static int arena_grow(size_t need) {
    size_t want = need + HDR_SIZE;
    if (want < ARENA_CHUNK) want = ARENA_CHUNK;
    want = align_up(want);

    void *prev = sbrk((long)want);
    if (prev == (void *)-1) return -1;

    unsigned char *start = (unsigned char *)prev;

    if (!arena_first) {
        size_t misalign = (size_t)((uintptr_t)start & (ALIGNMENT - 1));
        if (misalign) {
            size_t pad = ALIGNMENT - misalign;
            start += pad;
            want  -= pad;
        }
    } else if (start != arena_hi) {
        /* Another actor moved brk.  We cannot safely chain. */
        return -1;
    }

    arena_append(start, want);
    return 0;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size_t need = align_up(size);
    if (need < MIN_PAYLOAD) need = MIN_PAYLOAD;

    for (;;) {
        for (block_t *b = arena_first; b; b = blk_next(b)) {
            if (!blk_free(b) || blk_size(b) < need) continue;
            split_block(b, need);
            return (unsigned char *)b + HDR_SIZE;
        }

        if (arena_grow(need) < 0) {
            errno = ENOMEM;
            return NULL;
        }
    }
}

void free(void *ptr) {
    if (!ptr) return;

    unsigned char *p = (unsigned char *)ptr;
    if ((uintptr_t)p & (ALIGNMENT - 1)) return;           /* never ours */
    if (!arena_first) return;
    if (p < (unsigned char *)arena_first + HDR_SIZE) return;
    if (p >= arena_hi) return;

    block_t *b = (block_t *)(p - HDR_SIZE);
    if (blk_free(b)) return;                              /* double free */

    blk_set_free(b, 1);

    /* Coalesce forward. */
    block_t *nxt = blk_next(b);
    if (nxt && blk_free(nxt)) {
        blk_set_size(b, blk_size(b) + HDR_SIZE + blk_size(nxt), 1);
        block_t *after = blk_next(b);
        if (after) after->prev_size = blk_size(b);
        else       arena_last = b;
    }

    /* Coalesce backward. */
    block_t *prv = blk_prev(b);
    if (prv && blk_free(prv)) {
        blk_set_size(prv, blk_size(prv) + HDR_SIZE + blk_size(b), 1);
        block_t *after = blk_next(prv);
        if (after) after->prev_size = blk_size(prv);
        else       arena_last = prv;
    }
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }

    unsigned char *p = (unsigned char *)ptr;
    if ((uintptr_t)p & (ALIGNMENT - 1)) return NULL;
    if (!arena_first) return NULL;
    if (p < (unsigned char *)arena_first + HDR_SIZE) return NULL;
    if (p >= arena_hi) return NULL;

    block_t *b = (block_t *)(p - HDR_SIZE);
    size_t cur = blk_size(b);

    size_t need = align_up(size);
    if (need < MIN_PAYLOAD) need = MIN_PAYLOAD;

    if (cur >= need) {
        split_block(b, need);
        return ptr;
    }

    /* Try to grow in place by absorbing the following free block. */
    block_t *nxt = blk_next(b);
    if (nxt && blk_free(nxt)) {
        size_t merged = cur + HDR_SIZE + blk_size(nxt);
        if (merged >= need) {
            blk_set_size(b, merged, 0);
            block_t *after = blk_next(b);
            if (after) after->prev_size = merged;
            else       arena_last = b;
            split_block(b, need);
            return ptr;
        }
    }

    /* Fallback: allocate elsewhere, copy what fits, release the old block. */
    void *np = malloc(size);
    if (!np) return NULL;
    size_t copy = cur < size ? cur : size;
    memcpy(np, ptr, copy);
    free(ptr);
    return np;
}

void *calloc(size_t nmemb, size_t size) {
    if (size != 0 && nmemb > (size_t)-1 / size) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (!p) return NULL;
    memset(p, 0, total);
    return p;
}

void exit(int status) {
    _exit(status);
}

void abort(void) {
    _exit(134);
}
