#include <exyde/vmm.h>
#include <exyde/pmm.h>
#include <exyde/heap.h>
#include <exyde/panic.h>

extern u8 __boot_pml4[];

#define PTE_PRESENT   (1ull << 0)
#define PTE_WRITE     (1ull << 1)
#define PTE_USER      (1ull << 2)
#define PTE_PWT       (1ull << 3)
#define PTE_PCD       (1ull << 4)
#define PTE_HUGE      (1ull << 7)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull

/* Singleton for the bootstrap (kernel) address space.  pml4 is filled
 * on first use because __boot_pml4 is a linker symbol, not a constant
 * expression.  All current callers run single-threaded during boot. */
static struct vmm_space kernel_space;
static int kernel_space_ready;

static inline u64 *pml4_of(vmm_space_t s) {
    return (u64 *)(uintptr_t)s->pml4;
}

static inline size_t idx_pml4(vaddr_t va) { return (size_t)((va >> 39) & 0x1FF); }
static inline size_t idx_pdpt(vaddr_t va) { return (size_t)((va >> 30) & 0x1FF); }
static inline size_t idx_pd  (vaddr_t va) { return (size_t)((va >> 21) & 0x1FF); }
static inline size_t idx_pt  (vaddr_t va) { return (size_t)((va >> 12) & 0x1FF); }

static u64 flags_to_pte(u32 f) {
    u64 r = 0;
    if (f & VM_PRESENT) r |= PTE_PRESENT;
    if (f & VM_WRITE)   r |= PTE_WRITE;
    if (f & VM_USER)    r |= PTE_USER;
    if (f & VM_NOCACHE) r |= PTE_PWT | PTE_PCD;
    return r;
}

static u32 pte_to_flags(u64 e) {
    u32 f = 0;
    if (e & PTE_PRESENT)         f |= VM_PRESENT;
    if (e & PTE_WRITE)           f |= VM_WRITE;
    if (e & PTE_USER)            f |= VM_USER;
    if (e & (PTE_PWT | PTE_PCD)) f |= VM_NOCACHE;
    return f;
}

static void zero_page(u64 *p) {
    for (size_t i = 0; i < 512; ++i) p[i] = 0;
}

static u64 *descend_or_alloc(u64 *table, size_t idx, bool child_user) {
    u64 e = table[idx];
    if (e & PTE_PRESENT) {
        if (e & PTE_HUGE) return (u64 *)0;
        return (u64 *)(e & PTE_ADDR_MASK);
    }
    paddr_t np = pmm_alloc_page();
    if (np == 0) return (u64 *)0;
    u64 *child = (u64 *)(uintptr_t)np;
    zero_page(child);
    u64 flags = PTE_PRESENT | PTE_WRITE;
    if (child_user) flags |= PTE_USER;
    table[idx] = np | flags;
    return child;
}

vmm_space_t vmm_kernel_space(void) {
    if (!kernel_space_ready) {
        kernel_space.pml4      = (paddr_t)(uintptr_t)__boot_pml4;
        kernel_space.refcount  = 0xFFFFFFFFu;
        kernel_space.is_kernel = 1;
        kernel_space_ready     = 1;
    }
    return &kernel_space;
}

vmm_space_t vmm_create(void) {
    struct vmm_space *s = (struct vmm_space *)exy_zalloc(sizeof(*s));
    if (!s) return (vmm_space_t)0;

    paddr_t np = pmm_alloc_page();
    if (np == 0) { exy_free(s); return (vmm_space_t)0; }

    u64 *new_pml4 = (u64 *)(uintptr_t)np;
    zero_page(new_pml4);

    const u64 *kpml4 = (const u64 *)(uintptr_t)vmm_kernel_space()->pml4;
    for (size_t i = 0; i < 512; ++i) new_pml4[i] = kpml4[i];

    s->pml4      = np;
    s->refcount  = 1;
    s->is_kernel = 0;
    return s;
}

static void free_pt(u64 *pt) {
    for (size_t i = 0; i < 512; ++i) {
        u64 e = pt[i];
        if (!(e & PTE_PRESENT)) continue;
        if (e & PTE_HUGE) continue;
        pmm_free_page(e & PTE_ADDR_MASK);
    }
    pmm_free_page((paddr_t)(uintptr_t)pt);
}

static void free_pd(u64 *pd) {
    for (size_t i = 0; i < 512; ++i) {
        u64 e = pd[i];
        if (!(e & PTE_PRESENT)) continue;
        if (e & PTE_HUGE) { pmm_free_page(e & PTE_ADDR_MASK); continue; }
        free_pt((u64 *)(e & PTE_ADDR_MASK));
    }
    pmm_free_page((paddr_t)(uintptr_t)pd);
}

static void free_pdpt(u64 *pdpt) {
    for (size_t i = 0; i < 512; ++i) {
        u64 e = pdpt[i];
        if (!(e & PTE_PRESENT)) continue;
        if (e & PTE_HUGE) { pmm_free_page(e & PTE_ADDR_MASK); continue; }
        free_pd((u64 *)(e & PTE_ADDR_MASK));
    }
    pmm_free_page((paddr_t)(uintptr_t)pdpt);
}

/* Only called from vmm_space_unref when refcount reaches zero, on a
 * non-kernel space.  Frees PML4[1..511] subtree, the PML4 page, and
 * the struct itself.  PML4[0] is shared with the kernel. */
static void destroy_space(vmm_space_t space) {
    u64 *pml4 = pml4_of(space);
    for (size_t i = 1; i < 512; ++i) {
        u64 e = pml4[i];
        if (!(e & PTE_PRESENT)) continue;
        if (e & PTE_HUGE) { pmm_free_page(e & PTE_ADDR_MASK); continue; }
        free_pdpt((u64 *)(e & PTE_ADDR_MASK));
    }
    pmm_free_page(space->pml4);
    exy_free(space);
}

void vmm_space_ref(vmm_space_t space) {
    if (!space) return;
    if (space->is_kernel) return;
    space->refcount++;
}

void vmm_space_unref(vmm_space_t space) {
    if (!space) return;
    if (space->is_kernel) return;
    if (space->refcount == 0) return;   /* defensive: never destroy twice */
    space->refcount--;
    if (space->refcount == 0) destroy_space(space);
}

void vmm_switch(vmm_space_t s) {
    if (!s) return;
    __asm__ volatile("mov %0, %%cr3" :: "r"(s->pml4) : "memory");
}

void vmm_flush(vaddr_t va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

static u64 *walk_leaf(vmm_space_t space, vaddr_t va) {
    if (!space) return (u64 *)0;
    u64 *pml4 = pml4_of(space);

    u64 e = pml4[idx_pml4(va)];
    if (!(e & PTE_PRESENT) || (e & PTE_HUGE)) return (u64 *)0;
    u64 *pdpt = (u64 *)(e & PTE_ADDR_MASK);

    e = pdpt[idx_pdpt(va)];
    if (!(e & PTE_PRESENT) || (e & PTE_HUGE)) return (u64 *)0;
    u64 *pd = (u64 *)(e & PTE_ADDR_MASK);

    e = pd[idx_pd(va)];
    if (!(e & PTE_PRESENT) || (e & PTE_HUGE)) return (u64 *)0;

    return (u64 *)(e & PTE_ADDR_MASK);
}

static vaddr_t lower_bound(vmm_space_t space) {
    return space->is_kernel ? KERNEL_VA_BASE : USER_VA_BASE;
}

bool vmm_map(vmm_space_t space, vaddr_t va, paddr_t pa, u32 flags) {
    if (!space) return false;
    if (va & (PAGE_SIZE - 1)) return false;
    if (pa & (PAGE_SIZE - 1)) return false;
    if (va < lower_bound(space)) return false;
    if (!(flags & VM_PRESENT)) return false;

    bool user = (flags & VM_USER) != 0;

    u64 *pml4 = pml4_of(space);
    u64 *pdpt = descend_or_alloc(pml4, idx_pml4(va), user);
    if (!pdpt) return false;
    u64 *pd = descend_or_alloc(pdpt, idx_pdpt(va), user);
    if (!pd) return false;
    u64 *pt = descend_or_alloc(pd, idx_pd(va), user);
    if (!pt) return false;

    size_t i = idx_pt(va);
    if (pt[i] & PTE_PRESENT) return false;

    pt[i] = (pa & PTE_ADDR_MASK) | flags_to_pte(flags);
    vmm_flush(va);
    return true;
}

bool vmm_unmap(vmm_space_t space, vaddr_t va) {
    if (!space) return false;
    if (va & (PAGE_SIZE - 1)) return false;
    if (va < lower_bound(space)) return false;

    u64 *pt = walk_leaf(space, va);
    if (!pt) return false;

    size_t i = idx_pt(va);
    if (!(pt[i] & PTE_PRESENT)) return false;

    pt[i] = 0;
    vmm_flush(va);
    return true;
}

bool vmm_query(vmm_space_t space, vaddr_t va, paddr_t *out_pa, u32 *out_flags) {
    if (!space) return false;
    if (va & (PAGE_SIZE - 1)) return false;

    u64 *pt = walk_leaf(space, va);
    if (!pt) return false;

    u64 e = pt[idx_pt(va)];
    if (!(e & PTE_PRESENT)) return false;

    if (out_pa)    *out_pa    = e & PTE_ADDR_MASK;
    if (out_flags) *out_flags = pte_to_flags(e);
    return true;
}
