#include <stdlib.h>
#include <unistd.h>
#include <exyde/micro.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

/*
 * malloc / free / calloc / realloc -- Phase 11.1.3.
 *
 * Boundary-tag allocator over an mmap-backed arena.  The arena is
 * one contiguous 4 MiB region obtained once at first malloc via
 * exyde_map(); it never grows.  Every block is preceded by a
 * 16-byte header:
 *
 *     struct block { size_t size; size_t prev_size; };
 *
 *   - `size`      : payload size in bytes; low bit is the FREE flag.
 *   - `prev_size` : payload size of the preceding block, or 0 if this
 *                   is the first block on the arena.
 *
 * free() never returns pages to the kernel -- that would require an
 * unmap and a per-page tracking map.  It coalesces with adjacent
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

/* One-shot arena: mmap ARENA_SIZE bytes at first malloc and never
 * grow.  The boundary-tag walker assumes a contiguous region, and
 * SYS_MAP does not guarantee that successive calls return adjacent
 * pages.  4 MiB is plenty for the current test suite. */
#define ARENA_SIZE   ((size_t)4 * 1024 * 1024)
#define ARENA_PAGES  ((unsigned)(ARENA_SIZE / EXYDE_PAGE_SIZE))
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

/* Map the arena once.  Subsequent calls are no-ops after the first
 * successful or failed attempt.  exyde_map returns page-aligned
 * memory, so no misalign handling is needed. */
static int arena_init_once(void) {
    static int tried = 0;
    if (tried) return arena_first ? 0 : -1;
    tried = 1;

    void *p = exyde_map(NULL, ARENA_PAGES, EXYDE_MAP_WRITE);
    if (!p) return -1;
    arena_append((unsigned char *)p, ARENA_SIZE);
    return 0;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size_t need = align_up(size);
    if (need < MIN_PAYLOAD) need = MIN_PAYLOAD;

    if (!arena_first) {
        if (arena_init_once() < 0) {
            errno = ENOMEM;
            return NULL;
        }
    }

    for (block_t *b = arena_first; b; b = blk_next(b)) {
        if (!blk_free(b) || blk_size(b) < need) continue;
        split_block(b, need);
        return (unsigned char *)b + HDR_SIZE;
    }

    errno = ENOMEM;
    return NULL;
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

/* --- numeric conversion -------------------------------------------- */

/* Character classification helpers, mirroring <ctype.h> without
 * pulling a whole ctype into the libc yet. */
static int is_space_(int c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}
static int digit_val_(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    return -1;
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    const char *p = s;

    while (is_space_((unsigned char)*p)) ++p;

    int negative = 0;
    if (*p == '+' || *p == '-') {
        negative = (*p == '-');
        ++p;
    }

    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        } else if (p[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    } else if (base < 2 || base > 36) {
        if (endptr) *endptr = (char *)s;
        errno = EINVAL;
        return 0;
    }

    unsigned long acc = 0;
    unsigned long cutoff = ULONG_MAX / (unsigned long)base;
    unsigned long cutlim = ULONG_MAX % (unsigned long)base;
    int overflow = 0;
    int any = 0;

    for (;;) {
        int d = digit_val_((unsigned char)*p);
        if (d < 0 || d >= base) break;
        if (!overflow) {
            if (acc > cutoff || (acc == cutoff && (unsigned long)d > cutlim)) {
                overflow = 1;
                errno = ERANGE;
            } else {
                acc = acc * (unsigned long)base + (unsigned long)d;
            }
        }
        ++p;
        any = 1;
    }

    if (endptr) *endptr = (char *)(any ? p : s);

    if (overflow) return ULONG_MAX;
    return negative ? (unsigned long)(-(long long)acc) : acc;
}

long strtol(const char *s, char **endptr, int base) {
    /* Standalone parser: strtoul() returns the modular (two's-complement)
     * value for negative inputs, so delegating to it here would misread
     * "-17" as ULONG_MAX - 16 and trip the overflow check.  Parse the
     * magnitude here and apply the sign with LONG_MIN / LONG_MAX bounds. */
    const char *p = s;

    while (is_space_((unsigned char)*p)) ++p;

    int negative = 0;
    if (*p == '+' || *p == '-') {
        negative = (*p == '-');
        ++p;
    }

    int effective_base = base;
    if (effective_base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            effective_base = 16;
            p += 2;
        } else if (p[0] == '0') {
            effective_base = 8;
        } else {
            effective_base = 10;
        }
    } else if (effective_base == 16) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    } else if (effective_base < 2 || effective_base > 36) {
        if (endptr) *endptr = (char *)s;
        errno = EINVAL;
        return 0;
    }

    /* Magnitude limit: LONG_MAX for positive, LONG_MAX + 1 for negative
     * (so that LONG_MIN is representable). */
    unsigned long limit = negative
        ? (unsigned long)LONG_MAX + 1UL
        : (unsigned long)LONG_MAX;
    unsigned long cutoff = limit / (unsigned long)effective_base;
    unsigned long cutlim = limit % (unsigned long)effective_base;

    unsigned long acc = 0;
    int overflow = 0;
    int any = 0;

    for (;;) {
        int d = digit_val_((unsigned char)*p);
        if (d < 0 || d >= effective_base) break;
        if (!overflow) {
            if (acc > cutoff || (acc == cutoff && (unsigned long)d > cutlim)) {
                overflow = 1;
            } else {
                acc = acc * (unsigned long)effective_base + (unsigned long)d;
            }
        }
        ++p;
        any = 1;
    }

    if (endptr) *endptr = (char *)(any ? p : s);

    if (overflow) {
        errno = ERANGE;
        return negative ? LONG_MIN : LONG_MAX;
    }

    if (negative) {
        /* LONG_MIN is the only value where (long)(-acc) would be UB. */
        if (acc == (unsigned long)LONG_MAX + 1UL) return LONG_MIN;
        return -(long)acc;
    }
    return (long)acc;
}

int atoi(const char *s) {
    return (int)strtol(s, (char **)0, 10);
}

long atol(const char *s) {
    return strtol(s, (char **)0, 10);
}

/* --- integer arithmetic -------------------------------------------- */

int abs(int j) {
    return j < 0 ? -j : j;
}

long labs(long j) {
    return j < 0 ? -j : j;
}

/* --- qsort / bsearch ----------------------------------------------- */

/* Swap `size` bytes between a and b using a byte-wise temporary on the
 * stack.  Callers guarantee size is small (an element, not a buffer). */
static void swap_bytes(unsigned char *a, unsigned char *b, size_t size) {
    while (size--) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* In-place quicksort.  Iterative with an explicit stack so we do not
 * rely on recursion depth in userspace.  Median-of-three pivot choice
 * keeps the common sorted/nearly-sorted cases reasonable. */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    if (nmemb < 2 || size == 0 || !compar) return;

    unsigned char *lo = (unsigned char *)base;
    unsigned char *hi = lo + (nmemb - 1) * size;

    /* Stack of (lo, hi) ranges to process.  Each iteration consumes one
     * range and may push two smaller ones.  Depth is bounded by the
     * recursion depth of the same algorithm, which for median-of-three
     * is O(log n) on average and O(n) only on adversarial inputs.  We
     * cap it at 64 to stay safe; if we ever exceed it, the remaining
     * range is sorted with an insertion-sort fallback below. */
    struct range { unsigned char *lo; unsigned char *hi; };
    struct range stack[64];
    int sp = 0;
    stack[sp].lo = lo;
    stack[sp].hi = hi;
    ++sp;

    while (sp > 0) {
        --sp;
        unsigned char *l = stack[sp].lo;
        unsigned char *h = stack[sp].hi;

        /* Insertion-sort small ranges: cheap and avoids deep stacks. */
        if ((size_t)((h - l) / size) < 8) {
            for (unsigned char *i = l + size; i <= h; i += size) {
                for (unsigned char *j = i; j > l; j -= size) {
                    if (compar(j - size, j) <= 0) break;
                    swap_bytes(j - size, j, size);
                }
            }
            continue;
        }

        /* Median-of-three pivot. */
        unsigned char *mid = l + ((size_t)((h - l) / size) / 2) * size;
        if (compar(l, mid) > 0) swap_bytes(l, mid, size);
        if (compar(l, h)   > 0) swap_bytes(l, h,   size);
        if (compar(mid, h) > 0) swap_bytes(mid, h, size);
        swap_bytes(mid, h - size, size);   /* move pivot next to hi */

        unsigned char *pivot = h - size;
        unsigned char *i = l;
        unsigned char *j = h - size;

        for (;;) {
            while (i < j && compar(i, pivot) <= 0) i += size;
            while (j > i && compar(j, pivot) >= 0) j -= size;
            if (i >= j) break;
            swap_bytes(i, j, size);
            i += size;
            if (j > l) j -= size;
        }
        /* Put pivot back into place. */
        swap_bytes(i, pivot, size);

        /* Push subranges.  If the stack would overflow, fall back to
         * insertion sort on the whole range (never seen in practice). */
        if (sp + 2 > 64) {
            for (unsigned char *p = l + size; p <= h; p += size) {
                for (unsigned char *q = p; q > l; q -= size) {
                    if (compar(q - size, q) <= 0) break;
                    swap_bytes(q - size, q, size);
                }
            }
            continue;
        }

        if (i > l)      { stack[sp].lo = l;     stack[sp].hi = i - size; ++sp; }
        if (i < h)      { stack[sp].lo = i + size; stack[sp].hi = h;     ++sp; }
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
    if (!compar || size == 0 || nmemb == 0) return NULL;

    const unsigned char *lo = (const unsigned char *)base;
    const unsigned char *hi = lo + nmemb * size;

    while (lo < hi) {
        size_t n = (size_t)((hi - lo) / size);
        const unsigned char *mid = lo + (n / 2) * size;
        int c = compar(key, mid);
        if (c == 0) return (void *)mid;
        if (c < 0) hi = mid;
        else       lo = mid + size;
    }
    return NULL;
}

void exit(int status) {
    _exit(status);
}

void abort(void) {
    _exit(134);
}
