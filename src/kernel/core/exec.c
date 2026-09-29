#include <exyde/exec.h>
#include <exyde/process.h>
#include <exyde/sched.h>
#include <exyde/thread.h>
#include <exyde/vmm.h>
#include <exyde/pmm.h>
#include <exyde/heap.h>
#include <exyde/user.h>
#include <exyde/handle.h>
#include <exyde/arch.h>
#include <exyde/uaccess.h>
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

/* Map `size` bytes at INITRD_VA in `space`, read-only (no VM_WRITE).
 *
 * The newly mapped pages are only reachable through the target
 * space's user VAs, so we switch CR3 to `space` for the duration of
 * the copy and restore the caller's space afterwards.  On any
 * failure the function unwinds everything it managed to map and
 * returns false. */
static bool map_initrd(vmm_space_t space, const void *src, size_t size) {
    if (!src || size == 0) return true;

    vaddr_t base  = INITRD_VA;
    size_t  pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    /* The ELF loader has already run by the time we get here; make
     * sure our range does not collide with anything it placed. */
    for (size_t i = 0; i < pages; ++i) {
        paddr_t pa; u32 fl;
        if (vmm_query(space, base + (vaddr_t)i * PAGE_SIZE, &pa, &fl))
            return false;
    }

    vmm_space_t prev = thread_current()->space;
    bool need_switch = (prev != space);
    if (need_switch) vmm_switch(space);

    const u8 *ksrc   = (const u8 *)src;
    size_t    mapped = 0;
    bool      ok     = true;

    for (size_t i = 0; i < pages; ++i) {
        paddr_t pa = pmm_alloc_page();
        if (!pa) { ok = false; break; }

        vaddr_t va = base + (vaddr_t)i * PAGE_SIZE;
        if (!vmm_map(space, va, pa, VM_PRESENT | VM_USER)) {
            pmm_free_page(pa);
            ok = false;
            break;
        }
        ++mapped;

        u8    *udst   = (u8 *)va;
        size_t off    = i * PAGE_SIZE;
        size_t remain = (off < size) ? (size - off) : 0;
        if (remain > PAGE_SIZE) remain = PAGE_SIZE;

        for (size_t j = 0; j < PAGE_SIZE; ++j) udst[j] = 0;
        for (size_t j = 0; j < remain;    ++j) udst[j] = ksrc[off + j];
    }

    if (!ok) {
        for (size_t i = 0; i < mapped; ++i) {
            vaddr_t va = base + (vaddr_t)i * PAGE_SIZE;
            paddr_t pa;
            if (vmm_query(space, va, &pa, (u32 *)0)) {
                vmm_unmap(space, va);
                pmm_free_page(pa);
            }
        }
    }

    if (need_switch) vmm_switch(prev);
    return ok;
}

/* Copy bytes to a user VA.  The caller must have switched CR3 to
 * `space` for the duration of the build (see exec_build_initial_stack). */
static bool write_user_bytes(vmm_space_t space, vaddr_t va,
                             const void *src, size_t n) {
    return copy_to_user(space, va, src, n) == 0;
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
    if (!space) return 0;

    /* copy_to_user dereferences user VAs in the current address space,
     * so switch CR3 to the target for the duration of the build. */
    vmm_space_t prev = thread_current()->space;
    bool need_switch = (prev != space);
    if (need_switch) vmm_switch(space);

    vaddr_t rsp = stack_top;
    vaddr_t argv_va[EXEC_MAX_ARGS];
    vaddr_t envp_va[EXEC_MAX_ENVS];
    u64 result = 0;

    /* 1. Push strings first (high to low). */
    for (int i = envc - 1; i >= 0; --i)
        if (!push_string(space, &rsp, envp[i], &envp_va[i])) goto out;
    for (int i = argc - 1; i >= 0; --i)
        if (!push_string(space, &rsp, argv[i], &argv_va[i])) goto out;

    /* 2. Align down to 16 bytes.  This stays at or below rsp, so it
     *    can never overlap the strings we just pushed above. */
    rsp &= ~(vaddr_t)0xF;

    /* 3. Pointer arrays.  Total slots to push:
     *      envp NULL (1) + envc + argv NULL (1) + argc + argc (1)
     *    = argc + envc + 3 (plus optional pad below).
     *    Final RSP is 16-aligned iff the slot count is even. */
    if (((argc + envc + 3) & 1) == 1)
        if (!push_u64(space, &rsp, 0)) goto out;             /* pad */
    if (!push_u64(space, &rsp, 0)) goto out;                 /* envp NULL */
    for (int i = envc - 1; i >= 0; --i)
        if (!push_u64(space, &rsp, (u64)envp_va[i])) goto out;
    if (!push_u64(space, &rsp, 0)) goto out;                 /* argv NULL */
    for (int i = argc - 1; i >= 0; --i)
        if (!push_u64(space, &rsp, (u64)argv_va[i])) goto out;
    if (!push_u64(space, &rsp, (u64)argc)) goto out;

    result = (u64)rsp;

out:
    if (need_switch) vmm_switch(prev);
    return result;
}

/* ---- spawn ---------------------------------------------------------- */

static void user_thread_entry(void *arg) {
    process_t *p = (process_t *)arg;
    arch_enter_user_mode(p->entry, p->initial_rsp);
}

/* Shared spawn path.  process_spawn and process_spawn_init are thin
 * wrappers over this; only the initrd argument differs.  `initrd` may
 * be NULL, in which case the target space gets no initrd mapping. */
static process_t *spawn_common(const char *name,
                               const void *elf, size_t elf_size,
                               const void *initrd, size_t initrd_size,
                               int argc, const char *const *argv,
                               int envc, const char *const *envp) {
    process_t *p = process_create_from_elf(name, elf, elf_size);
    if (!p) return (process_t *)0;

    if (initrd && initrd_size) {
        if (!map_initrd(p->space, initrd, initrd_size)) {
            process_destroy(p);
            return (process_t *)0;
        }
    }

    vaddr_t stack_top = USER_STACK_TOP_INIT;
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
    /* Thread owns one reference to the space.  process_destroy drops
     * the process's reference; free_thread drops the thread's. */
    vmm_space_ref(p->space);
    /* Use p->name (heap copy) so t->name outlives the caller's stack
     * frame.  p was created with a copy of `name` inside
     * process_create_from_elf. */
    thread_t *t = thread_create_ex(user_thread_entry, p, p->name, p->space);
    if (t) {
        t->process      = p;
        p->main_thread  = t;
    } else {
        vmm_space_unref(p->space);
    }
    arch_irqs_restore(flags);

    if (!t) {
        process_destroy(p);
        return (process_t *)0;
    }
    return p;
}

process_t *process_spawn(const char *name,
                         const void *elf, size_t elf_size,
                         int argc, const char *const *argv,
                         int envc, const char *const *envp) {
    return spawn_common(name, elf, elf_size, (const void *)0, 0,
                        argc, argv, envc, envp);
}

process_t *process_spawn_init(const char *name,
                              const void *elf, size_t elf_size,
                              const void *initrd, size_t initrd_size,
                              int argc, const char *const *argv,
                              int envc, const char *const *envp) {
    return spawn_common(name, elf, elf_size, initrd, initrd_size,
                        argc, argv, envc, envp);
}
