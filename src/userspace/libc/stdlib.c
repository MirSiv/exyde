#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

/* Simple bump allocator over brk.  free() is a no-op: a real free
 * list with block headers arrives in 11.1.  malloc() reserves memory
 * from the kernel in 64 KiB chunks so the brk syscall isn't on the
 * hot path of every small allocation. */

#define MALLOC_CHUNK  ((size_t)65536)
#define MALLOC_ALIGN  ((size_t)16)

static unsigned char *heap_cur = NULL;
static unsigned char *heap_end = NULL;

static int heap_grow(size_t need) {
    size_t want = need > MALLOC_CHUNK ? need : MALLOC_CHUNK;
    void *prev = sbrk((long)want);
    if (prev == (void *)-1) return -1;
    if (heap_cur == NULL) heap_cur = (unsigned char *)prev;
    heap_end = (unsigned char *)prev + want;
    return 0;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    size_t need = (size + (MALLOC_ALIGN - 1)) & ~(MALLOC_ALIGN - 1);
    if (heap_cur == NULL || (size_t)(heap_end - heap_cur) < need) {
        if (heap_grow(need) < 0) {
            errno = ENOMEM;
            return NULL;
        }
    }
    void *p = heap_cur;
    heap_cur += need;
    return p;
}

void free(void *ptr) {
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size) {
    if (size != 0 && nmemb > (size_t)-1 / size) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total = nmemb * size;
    unsigned char *p = (unsigned char *)malloc(total);
    if (!p) return NULL;
    for (size_t i = 0; i < total; ++i) p[i] = 0;
    return p;
}

void exit(int status) {
    _exit(status);
}

void abort(void) {
    _exit(134);
}
