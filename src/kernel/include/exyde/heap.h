#ifndef EXYDE_HEAP_H
#define EXYDE_HEAP_H

#include <exyde/types.h>
#include <exyde/vmm.h>   /* for KERNEL_VA_BASE */

/* Kernel heap VA range.  Starts one MiB above the bootstrap identity map
 * (which ends at KERNEL_VA_BASE) and grows upward. */
#define KERNEL_HEAP_BASE (KERNEL_VA_BASE + 0x00100000ULL)   /* +1 MiB   */
#define KERNEL_HEAP_MAX  (KERNEL_VA_BASE + 0x10000000ULL)   /* +256 MiB */

/* Initialise heap state.  Must be called after vmm is usable
 * (i.e. after vmm_kernel_space() returns non-zero) and before any
 * call to exy_malloc. */
void  heap_init(void);

void *exy_malloc(size_t size);
void *exy_zalloc(size_t size);
void *exy_realloc(void *ptr, size_t new_size);
void  exy_free(void *ptr);

/* Ensure at least `bytes` of payload is available in the free list.
 * Grows the heap as needed.  Returns true on success. */
bool heap_reserve(size_t bytes);

/* Payload bytes reserved at boot.  Must cover the largest single
 * kernel allocation that may occur in normal operation.  The
 * largest such allocation is SYS_SPAWN_ELF's bounce buffer,
 * capped at SPAWN_ELF_MAX (4 MiB, see core/syscall.c), plus
 * slack for process_t / thread_t / name copies along the way. */
#define HEAP_BOOT_RESERVE  (4u * 1024u * 1024u + 256u * 1024u)

/* Introspection (diagnostics and tests). */
size_t heap_used_bytes(void);   /* payload bytes currently allocated to callers */
size_t heap_free_bytes(void);   /* payload bytes in the free list */
size_t heap_total_bytes(void);  /* mapped VA bytes in the heap region */

#endif /* EXYDE_HEAP_H */
