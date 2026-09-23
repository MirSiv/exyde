#ifndef EXYDE_VMM_H
#define EXYDE_VMM_H

#include <exyde/types.h>

#define VM_PRESENT   (1u << 0)
#define VM_WRITE     (1u << 1)
#define VM_USER      (1u << 2)
#define VM_NOCACHE   (1u << 3)

/* An address space.  Refcounted: a process owns one reference, and
 * each thread that has been given the space holds its own reference.
 * The kernel space is a special singleton with is_kernel = 1; both
 * vmm_space_ref and vmm_space_unref are no-ops on it. */
typedef struct vmm_space *vmm_space_t;

struct vmm_space {
    paddr_t pml4;
    u32     refcount;
    u32     is_kernel;
};

/* Lower bound of the kernel dynamic VA range (heap, future kernel
 * mappings).  Below this lies the bootstrap identity map. */
#define KERNEL_VA_BASE  0x40000000ULL

/* Lower bound of user-space VA.  Sits at PML4[1] (512 GiB), so user
 * mappings can never touch the kernel's PML4[0] subtree. */
#define USER_VA_BASE    0x0000008000000000ULL

/* The kernel address space (bootstrap page tables).  Always non-NULL. */
vmm_space_t vmm_kernel_space(void);

/* Create a new address space that shares the kernel PML4[0] subtree
 * with the kernel space.  Returns NULL on failure.  Refcount starts
 * at 1; the caller owns that reference. */
vmm_space_t vmm_create(void);

/* Load space->pml4 into CR3.  Passing NULL is a no-op. */
void vmm_switch(vmm_space_t space);

/* Map / unmap / query a single 4 KiB page.  va and pa must be 4 KiB
 * aligned.  For the kernel space, va must be >= KERNEL_VA_BASE; for
 * any other space, va must be >= USER_VA_BASE.  vmm_map fails if va
 * is already mapped. */
bool vmm_map(vmm_space_t space, vaddr_t va, paddr_t pa, u32 flags);
bool vmm_unmap(vmm_space_t space, vaddr_t va);
bool vmm_query(vmm_space_t space, vaddr_t va, paddr_t *out_pa, u32 *out_flags);

/* Invalidate the TLB entry for `va` in the current address space. */
void vmm_flush(vaddr_t va);

/* Refcount management.  vmm_space_ref bumps the count; vmm_space_unref
 * drops it and, at zero, frees all leaf pages and intermediate tables
 * in PML4[1..511] plus the PML4 page and the struct itself.  PML4[0]
 * is shared with the kernel and is left alone.
 *
 * Both functions are no-ops for the kernel space. */
void vmm_space_ref(vmm_space_t space);
void vmm_space_unref(vmm_space_t space);

#endif /* EXYDE_VMM_H */
