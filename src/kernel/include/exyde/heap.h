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

/* Introspection (diagnostics and tests). */
size_t heap_used_bytes(void);   /* payload bytes currently allocated to callers */
size_t heap_free_bytes(void);   /* payload bytes in the free list */
size_t heap_total_bytes(void);  /* mapped VA bytes in the heap region */

#endif /* EXYDE_HEAP_H */
