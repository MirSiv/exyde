#include <exyde/exec.h>
#include <exyde/process.h>
#include <exyde/sched.h>
#include <exyde/thread.h>
#include <exyde/vmm.h>
#include <exyde/pmm.h>
#include <exyde/heap.h>
#include <exyde/user.h>
#include <exyde/condev.h>
#include <exyde/fd.h>
#include <exyde/handle.h>
#include <exyde/arch.h>
#include <exyde/panic.h>

/* ---- small helpers -------------------------------------------------- */

static size_t xs_strlen(const char *s) {
    size_t n = 0; while (s[n]) ++n; return n;
}

/* Map `pages` fresh physical pages ending at stack_top in user space. */
static bool map_user_stack(vmm_space_t space, vaddr_t stack_top, u32 pages) {
    for (u32 i = 0; i < pages; ++i) {
        paddr_t pa = pmm_alloc_page();
        if (!pa) return false;
        vaddr_t va = stack_top - (vaddr_t)(i + 1) * PAGE_SIZE;
        if (!vmm_map(space, va, pa, VM_PRESENT | VM_WRITE | VM_USER)) {
            pmm_free_page(pa);
            return false;
        }
    }
    return true;
}

/* Write bytes to a user VA.  The user pages are reached through the
 * bootstrap identity map, which covers [0, 1 GiB) -- exactly the range
 * PMM hands out. */
static bool write_user_bytes(vmm_space_t space, vaddr_t va,
                             const void *src, size_t n) {
    const u8 *s = (const u8 *)src;
    while (n > 0) {
        paddr_t pa;
        u32 flags;
        vaddr_t page = va & ~(vaddr_t)(PAGE_SIZE - 1);
        if (!vmm_query(space, page, &pa, &flags)) return false;
        u64 page_off = va & (vaddr_t)(PAGE_SIZE - 1);
        size_t chunk = PAGE_SIZE - page_off;
        if (chunk > n) chunk = n;
        u8 *dst = (u8 *)(uintptr_t)(pa + page_off);
        for (size_t i = 0; i < chunk; ++i) dst[i] = s[i];
        s += chunk;
        va += chunk;
        n -= chunk;
    }
    return true;
}

static bool push_u64(vmm_space_t space, vaddr_t *rsp, u64 v) {
    *rsp -= 8;
    return write_user_bytes(space, *rsp, &v, 8);
}

static bool push_string(vmm_space_t space, vaddr_t *rsp,
                        const char *str, vaddr_t *out_va) {
    size_t len = xs_strlen(str) + 1;
    *rsp -= len;
    if (!write_user_bytes(space, *rsp, str, len)) return false;
    *out_va = *rsp;
    return true;
}

/* ---- initial stack -------------------------------------------------- */

u64 exec_build_initial_stack(vmm_space_t space, vaddr_t stack_top,
                             int argc, const char *const *argv,
                             int envc, const char *const *envp) {
    if (argc < 0 || argc > EXEC_MAX_ARGS) return 0;
    if (envc < 0 || envc > EXEC_MAX_ENVS) return 0;

    vaddr_t rsp = stack_top;
    vaddr_t argv_va[EXEC_MAX_ARGS];
    vaddr_t envp_va[EXEC_MAX_ENVS];

    /* 1. Push strings first (high to low). */
    for (int i = envc - 1; i >= 0; --i)
        if (!push_string(space, &rsp, envp[i], &envp_va[i])) return 0;
    for (int i = argc - 1; i >= 0; --i)
        if (!push_string(space, &rsp, argv[i], &argv_va[i])) return 0;

    /* 2. Align down to 16 bytes.  This stays at or below rsp, so it
     *    can never overlap the strings we just pushed above. */
    rsp &= ~(vaddr_t)0xF;

    /* 3. Pointer arrays.  Total slots to push:
     *      envp NULL (1) + envc + argv NULL (1) + argc + argc (1)
     *    = argc + envc + 3 (plus optional pad below).
     *    Final RSP is 16-aligned iff the slot count is even. */
    if (((argc + envc + 3) & 1) == 1)
        if (!push_u64(space, &rsp, 0)) return 0;             /* pad */
    if (!push_u64(space, &rsp, 0)) return 0;                 /* envp NULL */
    for (int i = envc - 1; i >= 0; --i)
        if (!push_u64(space, &rsp, (u64)envp_va[i])) return 0;
    if (!push_u64(space, &rsp, 0)) return 0;                 /* argv NULL */
    for (int i = argc - 1; i >= 0; --i)
        if (!push_u64(space, &rsp, (u64)argv_va[i])) return 0;
    if (!push_u64(space, &rsp, (u64)argc)) return 0;

    return (u64)rsp;
}

/* ---- spawn ---------------------------------------------------------- */

static void user_thread_entry(void *arg) {
    process_t *p = (process_t *)arg;
    arch_enter_user_mode(p->image.entry, p->initial_rsp);
}

static bool process_open_console_fds(process_t *p) {
    vnode_t *cons = condev_ref();
    if (!cons) return false;

    for (int i = 0; i < 3; ++i) {
        if (i > 0) vnode_ref(cons);   /* fd 0 already holds the first ref */
        file_t *f = (file_t *)kzalloc(sizeof(file_t));
        if (!f) { vnode_unref(cons); return false; }
        f->vn       = cons;
        f->offset   = 0;
        f->flags    = VFS_O_RDWR;
        f->refcount = 1;
        if (fd_alloc(&p->fds, f) != i) {
            vfs_close(f);
            return false;
        }
    }
    return true;
}

process_t *process_spawn(const char *name,
                         const void *elf, size_t elf_size,
                         int argc, const char *const *argv,
                         int envc, const char *const *envp) {
    process_t *p = process_create_from_elf(name, elf, elf_size);
    if (!p) return (process_t *)0;

    if (!process_open_console_fds(p)) {
        process_destroy(p);
        return (process_t *)0;
    }

    vaddr_t stack_top = USER_VA_BASE + 0x00100000ULL;
    if (!map_user_stack(p->space, stack_top, EXEC_STACK_PAGES)) {
        process_destroy(p);
        return (process_t *)0;
    }

    u64 init_rsp = exec_build_initial_stack(p->space, stack_top,
                                            argc, argv, envc, envp);
    if (!init_rsp) {
        process_destroy(p);
        return (process_t *)0;
    }
    p->initial_rsp = init_rsp;

    /* Close the preemption window between enqueue and t->process = p. */
    u64 flags = arch_irqs_save_and_disable();
    thread_t *t = thread_create_ex(user_thread_entry, p, name, p->space);
    if (t) {
        t->process      = p;
        p->main_thread  = t;
    }
    arch_irqs_restore(flags);

    if (!t) {
        process_destroy(p);
        return (process_t *)0;
    }
    return p;
}
