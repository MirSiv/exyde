#include <exyde/syscall.h>
#include <exyde/errno.h>
#include <exyde/handle.h>
#include <exyde/console.h>
#include <exyde/thread.h>
#include <exyde/sched.h>
#include <exyde/process.h>
#include <exyde/uaccess.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
#include <exyde/exec.h>
#include <exyde/channel.h>
#include <exyde/arch.h>
#include <exyde/exec.h>
#include <exyde/elf_table.h>
#include <exyde/panic.h>

/* One-shot kernel bounce buffer for SYS_KPUTS.
 * Bigger requests are rejected with -EINVAL. */
#define SYSCALL_IO_MAX  4096u

/* Upper bounds for the microkernel IPC primitive.  Raised in
 * Phase 11.5.3 so the VFS RPC protocol (struct vfs_req/rsp, up to
 * 1024 bytes) fits in a single message.  1024 bytes sits comfortably
 * on a 16 KiB kernel stack; if this grows further, switch the bounce
 * buffer in sys_ipc_send/recv to a heap allocation. */
#define IPC_MSG_SIZE_MAX  1024u
#define IPC_CAPACITY_MAX  64u

/* Capability action codes for SYS_IPC_SEND_CAP. */
#define IPC_CAP_TRANSFER   1u
#define IPC_CAP_DUPLICATE  2u

/* Upper bound on a single SYS_MAP request (in 4 KiB pages).  Raised
 * in Phase 11.5.6c so the libc allocator can map its 4 MiB arena in
 * one call. */
#define MAP_MAX_PAGES  1024u

static void kcopy(u8 *dst, const u8 *src, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
}

static process_t *current_process(void) {
    thread_t *t = thread_current();
    if (!t || !t->process) return (process_t *)0;
    return (process_t *)t->process;
}

/* ==================================================================== */
/* Microkernel core                                                     */
/* ==================================================================== */

static sysret_t sys_ping(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    return (sysret_t)(0xDEADBEEFCAFE0000ULL ^ a0);
}

static sysret_t sys_exit(u64 code, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (p) process_exit(p, (int)(i32)code);
    thread_exit();
}

static sysret_t sys_handle_create(u64 kind, u64 rights, u64 object,
                                  u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    handle_t h = handle_create(&p->handles,
                               (u32)kind, (u32)rights,
                               (void *)(uintptr_t)object);
    if (h == HANDLE_INVALID) return SYSRET_ERR(EMFILE);
    return (sysret_t)h;
}

static sysret_t sys_handle_close(u64 h, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (!handle_close(&p->handles, (handle_t)h)) return SYSRET_ERR(EBADF);
    return 0;
}

static sysret_t sys_handle_query(u64 h, u64 required, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (!handle_lookup(&p->handles, (handle_t)h, (u32)required,
                       (void **)0, (u32 *)0)) {
        return SYSRET_ERR(EPERM);
    }
    return 1;
}

static sysret_t sys_getpid(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    return (sysret_t)p->pid;
}

/* ---- IPC ----------------------------------------------------------- */

static channel_t *lookup_channel(process_t *p, handle_t h, u32 required) {
    void *obj = (void *)0;
    u32   kind = 0;
    if (!handle_lookup(&p->handles, h, required, &obj, &kind)) return (channel_t *)0;
    if (kind != HANDLE_KIND_CHANNEL) return (channel_t *)0;
    return (channel_t *)obj;
}

static sysret_t sys_ipc_create(u64 msg_size, u64 capacity,
                               u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (msg_size == 0 || msg_size > IPC_MSG_SIZE_MAX) return SYSRET_ERR(EINVAL);
    if (capacity == 0 || capacity > IPC_CAPACITY_MAX) return SYSRET_ERR(EINVAL);

    channel_t *c = channel_create((size_t)capacity, (size_t)msg_size);
    if (!c) return SYSRET_ERR(ENOMEM);

    /* Channel handles get TRANSFER by default: without it a channel
     * created here could never be shared via IPC, which defeats the
     * point of SYS_IPC_CREATE.  A caller that wants to forbid
     * transfer can mint a fresh, narrower handle via SYS_HANDLE_CREATE
     * (currently only HANDLE_KIND_TEST is supported for that). */
    handle_t h = handle_create(&p->handles, HANDLE_KIND_CHANNEL,
                               HANDLE_RIGHT_READ  | HANDLE_RIGHT_WRITE |
                               HANDLE_RIGHT_TRANSFER, c);
    if (h == HANDLE_INVALID) {
        channel_destroy(c);
        return SYSRET_ERR(EMFILE);
    }
    return (sysret_t)h;
}

static sysret_t sys_ipc_send(u64 h, u64 buf_u, u64 len,
                             u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    channel_t *c = lookup_channel(p, (handle_t)h, HANDLE_RIGHT_WRITE);
    if (!c) return SYSRET_ERR(EBADF);
    if (len != c->msg_size) return SYSRET_ERR(EINVAL);

    u8 kbuf[IPC_MSG_SIZE_MAX];
    if (copy_from_user(p->space, kbuf, (vaddr_t)buf_u, (size_t)len) < 0)
        return SYSRET_ERR(EFAULT);

    channel_send(c, kbuf);
    return 0;
}

static sysret_t sys_ipc_try_send(u64 h, u64 buf_u, u64 len,
                                 u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    channel_t *c = lookup_channel(p, (handle_t)h, HANDLE_RIGHT_WRITE);
    if (!c) return SYSRET_ERR(EBADF);
    if (len != c->msg_size) return SYSRET_ERR(EINVAL);

    u8 kbuf[IPC_MSG_SIZE_MAX];
    if (copy_from_user(p->space, kbuf, (vaddr_t)buf_u, (size_t)len) < 0)
        return SYSRET_ERR(EFAULT);

    if (!channel_try_send(c, kbuf)) return SYSRET_ERR(EAGAIN);
    return 0;
}

/* Plain recv refuses (EINVAL) a message that carries a capability.
 * The message stays in the channel; the caller must use RECV_CAP. */
static sysret_t sys_ipc_recv(u64 h, u64 buf_u, u64 max_len,
                             u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    channel_t *c = lookup_channel(p, (handle_t)h, HANDLE_RIGHT_READ);
    if (!c) return SYSRET_ERR(EBADF);
    if (max_len < c->msg_size) return SYSRET_ERR(EINVAL);

    arch_irqs_disable();
    u8 *slot; chan_cap_t *cs;
    channel_peek_blocking(c, &slot, &cs);
    if (cs->in_use) {
        arch_irqs_enable();
        return SYSRET_ERR(EINVAL);
    }
    u8 kbuf[IPC_MSG_SIZE_MAX];
    kcopy(kbuf, slot, c->msg_size);
    channel_commit(c);
    arch_irqs_enable();

    if (copy_to_user(p->space, (vaddr_t)buf_u, kbuf, c->msg_size) < 0)
        return SYSRET_ERR(EFAULT);
    return (sysret_t)c->msg_size;
}

static sysret_t sys_ipc_try_recv(u64 h, u64 buf_u, u64 max_len,
                                 u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    channel_t *c = lookup_channel(p, (handle_t)h, HANDLE_RIGHT_READ);
    if (!c) return SYSRET_ERR(EBADF);
    if (max_len < c->msg_size) return SYSRET_ERR(EINVAL);

    arch_irqs_disable();
    u8 *slot; chan_cap_t *cs;
    if (channel_peek_try(c, &slot, &cs) != 0) {
        arch_irqs_enable();
        return SYSRET_ERR(EAGAIN);
    }
    if (cs->in_use) {
        arch_irqs_enable();
        return SYSRET_ERR(EINVAL);
    }
    u8 kbuf[IPC_MSG_SIZE_MAX];
    kcopy(kbuf, slot, c->msg_size);
    channel_commit(c);
    arch_irqs_enable();

    if (copy_to_user(p->space, (vaddr_t)buf_u, kbuf, c->msg_size) < 0)
        return SYSRET_ERR(EFAULT);
    return (sysret_t)c->msg_size;
}

/* ---- IPC with capability transfer ---------------------------------- */

/* Sender-side.  cap_h == HANDLE_INVALID means plain send (no cap). */
static sysret_t sys_ipc_send_cap(u64 h, u64 buf_u, u64 len,
                                 u64 cap_h, u64 action) {
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    channel_t *c = lookup_channel(p, (handle_t)h, HANDLE_RIGHT_WRITE);
    if (!c) return SYSRET_ERR(EBADF);
    if (len != c->msg_size) return SYSRET_ERR(EINVAL);

    u8 kbuf[IPC_MSG_SIZE_MAX];
    if (copy_from_user(p->space, kbuf, (vaddr_t)buf_u, (size_t)len) < 0)
        return SYSRET_ERR(EFAULT);

    if (cap_h == (u64)HANDLE_INVALID) {
        channel_send_cap(c, kbuf, 0, 0, (void *)0, false, false);
        return 0;
    }

    if (action != IPC_CAP_TRANSFER && action != IPC_CAP_DUPLICATE)
        return SYSRET_ERR(EINVAL);

    void *obj = (void *)0;
    u32   kind = 0, rights = 0;
    if (!handle_lookup_full(&p->handles, (handle_t)cap_h,
                            HANDLE_RIGHT_TRANSFER, &obj, &kind, &rights))
        return SYSRET_ERR(EPERM);

    /* On TRANSFER the receiver becomes the new owner; on DUPLICATE
     * the sender stays the owner and the receiver's copy has
     * owns_object == 0.  Without this, if sender and receiver live
     * in the same process (same handle table), the same object would
     * have two owns_object == 1 entries and table_destroy would
     * release it twice. */
    bool cap_owns = (action == IPC_CAP_TRANSFER);
    channel_send_cap(c, kbuf, kind, rights, obj, true, cap_owns);

    if (action == IPC_CAP_TRANSFER) {
        handle_disown(&p->handles, (handle_t)cap_h);
        handle_close(&p->handles, (handle_t)cap_h);
    }
    return 0;
}

/* Receiver-side, shared between blocking and non-blocking.  Pre-check
 * of the receiver's handle table happens BEFORE consuming the message
 * so a full table never silently destroys an incoming capability. */
static sysret_t recv_cap_common(u64 h, u64 buf_u, u64 max_len,
                                u64 out_cap_u, bool blocking) {
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    channel_t *c = lookup_channel(p, (handle_t)h, HANDLE_RIGHT_READ);
    if (!c) return SYSRET_ERR(EBADF);
    if (max_len < c->msg_size) return SYSRET_ERR(EINVAL);

    arch_irqs_disable();
    u8 *slot; chan_cap_t *cs;
    int r = blocking ? channel_peek_blocking(c, &slot, &cs)
                     : channel_peek_try(c, &slot, &cs);
    if (r != 0) {
        arch_irqs_enable();
        return SYSRET_ERR(EAGAIN);
    }

    bool has_cap = (cs->in_use != 0);
    if (has_cap && !handle_table_has_free_slot(&p->handles)) {
        arch_irqs_enable();
        return SYSRET_ERR(ENFILE);
    }

    u32   ck = 0, cr = 0;
    void *co = (void *)0;
    bool  cw = false;
    if (has_cap) {
        ck = cs->kind;
        cr = cs->rights;
        co = cs->object;
        cw = (cs->owns != 0);
    }

    handle_t new_h = HANDLE_INVALID;
    if (has_cap) {
        new_h = handle_create_ex(&p->handles, ck, cr, co, cw);
        if (new_h == HANDLE_INVALID) {
            arch_irqs_enable();
            return SYSRET_ERR(ENFILE);
        }
    }

    u8 kbuf[IPC_MSG_SIZE_MAX];
    kcopy(kbuf, slot, c->msg_size);
    channel_commit(c);
    arch_irqs_enable();

    if (copy_to_user(p->space, (vaddr_t)buf_u, kbuf, c->msg_size) < 0)
        return SYSRET_ERR(EFAULT);

    handle_t out_val = has_cap ? new_h : (handle_t)HANDLE_INVALID;
    if (copy_to_user(p->space, (vaddr_t)out_cap_u,
                     &out_val, sizeof(out_val)) < 0)
        return SYSRET_ERR(EFAULT);

    return (sysret_t)c->msg_size;
}

static sysret_t sys_ipc_recv_cap(u64 h, u64 buf_u, u64 max_len,
                                 u64 out_cap_u, u64 a4) {
    (void)a4;
    return recv_cap_common(h, buf_u, max_len, out_cap_u, true);
}

static sysret_t sys_ipc_try_recv_cap(u64 h, u64 buf_u, u64 max_len,
                                     u64 out_cap_u, u64 a4) {
    (void)a4;
    return recv_cap_common(h, buf_u, max_len, out_cap_u, false);
}

/* ---- memory primitives --------------------------------------------- */

/* Find a fresh VA range of `npages` consecutive unmapped pages for
 * hint==0.  Walks forward from p->next_map_va, skipping any pages that
 * are already mapped in this address space.  That is required because
 * init.elf uses both brk (via malloc) and SYS_MAP: brk grows the
 * region right after the image, and next_map_va also starts there.
 * Without the skip, the first SYS_MAP after any substantial malloc
 * would collide with a brk-mapped page and fail with ENOMEM.
 *
 * Returns 0 if no such range exists below the user stack. */
static vaddr_t find_map_va(process_t *p, size_t npages) {
    vaddr_t bytes = (vaddr_t)npages * PAGE_SIZE;
    if (bytes > USER_STACK_TOP_INIT) return 0;
    vaddr_t limit = USER_STACK_TOP_INIT - bytes;

    vaddr_t cand = (p->next_map_va + PAGE_SIZE - 1) & ~((vaddr_t)PAGE_SIZE - 1);
    while (cand <= limit) {
        int free_here = 1;
        for (size_t i = 0; i < npages; ++i) {
            paddr_t pa; u32 f;
            if (vmm_query(p->space, cand + (vaddr_t)i * PAGE_SIZE, &pa, &f)) {
                /* Collision at page i: jump past it and retry. */
                vaddr_t nxt = cand + (vaddr_t)(i + 1) * PAGE_SIZE;
                cand = (nxt + PAGE_SIZE - 1) & ~((vaddr_t)PAGE_SIZE - 1);
                free_here = 0;
                break;
            }
        }
        if (free_here) return cand;
    }
    return 0;
}

static sysret_t sys_map(u64 hint, u64 npages, u64 flags,
                        u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (npages == 0 || npages > MAP_MAX_PAGES) return SYSRET_ERR(EINVAL);
    /* Only VM_WRITE is accepted from userspace for now.  VM_PRESENT
     * and VM_USER are added by the kernel. */
    if (flags & ~(u64)VM_WRITE) return SYSRET_ERR(EINVAL);

    u32 vm_flags = VM_PRESENT | VM_USER;
    if (flags & VM_WRITE) vm_flags |= VM_WRITE;

    vaddr_t base;
    if (hint == 0) {
        base = find_map_va(p, (size_t)npages);
        if (base == 0) return SYSRET_ERR(ENOMEM);
    } else {
        base = (vaddr_t)hint;
        if (base < USER_VA_BASE) return SYSRET_ERR(EINVAL);
        if (base & (PAGE_SIZE - 1)) return SYSRET_ERR(EINVAL);
        vaddr_t end = base + (vaddr_t)npages * PAGE_SIZE;
        if (end < base || end > USER_VA_TOP) return SYSRET_ERR(EINVAL);
        for (u64 i = 0; i < npages; ++i) {
            paddr_t pa; u32 f;
            if (vmm_query(p->space, base + i * PAGE_SIZE, &pa, &f))
                return SYSRET_ERR(EEXIST);
        }
    }

    u64 mapped = 0;
    for (u64 i = 0; i < npages; ++i) {
        paddr_t pa = pmm_alloc_page();
        if (!pa) goto fail;
        if (!vmm_map(p->space, base + i * PAGE_SIZE, pa, vm_flags)) {
            pmm_free_page(pa);
            goto fail;
        }
        ++mapped;
    }

    if (hint == 0) p->next_map_va = base + (vaddr_t)npages * PAGE_SIZE;
    return (sysret_t)base;

fail:
    for (u64 i = 0; i < mapped; ++i) {
        paddr_t pa;
        if (vmm_query(p->space, base + i * PAGE_SIZE, &pa, (u32 *)0)) {
            vmm_unmap(p->space, base + i * PAGE_SIZE);
            pmm_free_page(pa);
        }
    }
    return SYSRET_ERR(ENOMEM);
}

static sysret_t sys_unmap(u64 vaddr, u64 npages, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (npages == 0) return SYSRET_ERR(EINVAL);

    vaddr_t base = (vaddr_t)vaddr;
    if (base < USER_VA_BASE) return SYSRET_ERR(EINVAL);
    if (base & (PAGE_SIZE - 1)) return SYSRET_ERR(EINVAL);
    vaddr_t end = base + (vaddr_t)npages * PAGE_SIZE;
    if (end < base || end > USER_VA_TOP) return SYSRET_ERR(EINVAL);

    /* Require the whole range to be mapped, so a partial unmap is
     * never performed. */
    for (u64 i = 0; i < npages; ++i) {
        paddr_t pa; u32 f;
        if (!vmm_query(p->space, base + i * PAGE_SIZE, &pa, &f))
            return SYSRET_ERR(ENOENT);
    }

    for (u64 i = 0; i < npages; ++i) {
        vaddr_t va = base + i * PAGE_SIZE;
        paddr_t pa;
        if (vmm_query(p->space, va, &pa, (u32 *)0)) {
            vmm_unmap(p->space, va);
            vmm_flush(va);
            pmm_free_page(pa);
        }
    }
    return 0;
}

static sysret_t sys_yield(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    sched_yield();
    return 0;
}

/* ---- process management (Phase 11.5.2) ----------------------------- */

#define SPAWN_NAME_MAX  64

/* Spawn a process from the kernel's embedded ELF table.
 *
 *   name_u   : user VA of a NUL-terminated name ("init", "test", ...)
 *   argv_u   : user VA of argv array; may be 0 for "no arguments".
 *              (Arg passing from userspace arrives in 11.5.3; for now
 *              argv is built internally as { name, NULL }.)
 *   argc     : accepted for API symmetry; must be 0.
 *   cap_h    : handle in the caller's table that will be duplicated
 *              into the child's table and stored as its
 *              bootstrap_handle.  HANDLE_INVALID means "no bootstrap".
 *   flags    : must be 0 for now.
 *
 * Returns a fresh handle on the child process (HANDLE_KIND_PROCESS),
 * or -errno.  The handle holds a process reference; closing it or
 * exiting the parent releases the reference, destroying the child
 * once its own main thread has also exited.
 *
 * IRQs are disabled for the duration to prevent the timer from
 * scheduling the child before its handles are installed. */
static sysret_t sys_spawn(u64 name_u, u64 argv_u, u64 argc,
                          u64 cap_h, u64 flags) {
    (void)argv_u;
    process_t *parent = current_process();
    if (!parent) return SYSRET_ERR(EPERM);
    if (flags != 0) return SYSRET_ERR(EINVAL);
    if (argc != 0)  return SYSRET_ERR(EINVAL);

    char name[SPAWN_NAME_MAX];
    int sr = strncpy_from_user(parent->space, name, (vaddr_t)name_u,
                               sizeof(name));
    if (sr < 0) return SYSRET_ERR((u64)-sr);

    const elf_entry_t *elf = elf_table_lookup(name);
    if (!elf || !elf->blob_start || elf_entry_size(elf) == 0)
        return SYSRET_ERR(ENOENT);

    /* Validate the bootstrap handle before touching anything heavy. */
    if (cap_h != (u64)HANDLE_INVALID) {
        void *obj = (void *)0; u32 kind = 0;
        if (!handle_lookup(&parent->handles, (handle_t)cap_h,
                           HANDLE_RIGHT_TRANSFER, &obj, &kind))
            return SYSRET_ERR(EPERM);
    }

    /* The child needs room for at least the process handle in the
     * parent's table; give ENFILE if the parent is full. */
    if (!handle_table_has_free_slot(&parent->handles))
        return SYSRET_ERR(EMFILE);

    u64 irq = arch_irqs_save_and_disable();

    const char *argv[] = { name, (const char *)0 };
    const char *envp[] = { "PATH=/", (const char *)0 };

    process_t *child = process_spawn(name, elf->blob_start,
                                     (size_t)elf_entry_size(elf),
                                     1, argv, 1, envp);
    if (!child) {
        arch_irqs_restore(irq);
        return SYSRET_ERR(ENOMEM);
    }

    /* Duplicate the caller's bootstrap handle into the child. */
    if (cap_h != (u64)HANDLE_INVALID) {
        void *obj = (void *)0; u32 kind = 0, rights = 0;
        /* lookup_full for rights */
        if (!handle_lookup_full(&parent->handles, (handle_t)cap_h,
                                HANDLE_RIGHT_TRANSFER, &obj, &kind, &rights)) {
            /* Should not happen: we validated above. */
            process_unref(child);
            arch_irqs_restore(irq);
            return SYSRET_ERR(EPERM);
        }
        /* DUPLICATE semantics: parent keeps owning the object; the
         * child gets a non-owning copy.  Only the parent's handle
         * releases the channel_t when closed, so closing both the
         * parent's and the child's copy does not double-free. */
        handle_t child_h = handle_create_ex(&child->handles, kind, rights,
                                            obj, false);
        if (child_h == HANDLE_INVALID) {
            process_unref(child);
            arch_irqs_restore(irq);
            return SYSRET_ERR(EMFILE);
        }
        child->bootstrap_handle = child_h;
    }

    /* Hand the parent a handle on the child.  Takes a ref. */
    process_ref(child);
    handle_t ph = handle_create(&parent->handles, HANDLE_KIND_PROCESS,
                                HANDLE_RIGHT_READ, child);
    if (ph == HANDLE_INVALID) {
        process_unref(child);
        process_unref(child);
        arch_irqs_restore(irq);
        return SYSRET_ERR(EMFILE);
    }

    arch_irqs_restore(irq);
    return (sysret_t)ph;
}

/* Block until the child whose handle is proc_h has called SYS_EXIT.
 * Returns its exit code (i32, may be negative).  Closing the handle
 * later drops the process's last reference. */
static sysret_t sys_wait(u64 proc_h, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *parent = current_process();
    if (!parent) return SYSRET_ERR(EPERM);

    void *obj = (void *)0; u32 kind = 0;
    if (!handle_lookup(&parent->handles, (handle_t)proc_h,
                       HANDLE_RIGHT_READ, &obj, &kind))
        return SYSRET_ERR(EBADF);
    if (kind != HANDLE_KIND_PROCESS) return SYSRET_ERR(EINVAL);

    process_t *child = (process_t *)obj;

    arch_irqs_disable();
    while (!child->exited) {
        waitq_push(&child->waiters, thread_current());
        sched_block();
    }
    int code = child->exit_code;
    arch_irqs_enable();

    return (sysret_t)(i64)code;
}

/* Return the bootstrap handle that the parent passed to SYS_SPAWN,
 * or -ENOENT if none was provided. */
static sysret_t sys_get_bootstrap(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (p->bootstrap_handle == HANDLE_INVALID) return SYSRET_ERR(ENOENT);
    return (sysret_t)p->bootstrap_handle;
}

/* SYS_KPUTS: write `count` bytes from user memory to the kernel
 * console.  No fd, no VFS.  Bounce buffer on the stack, 4 KiB cap.
 * Permanent low-level debug primitive. */
static sysret_t sys_kputs(u64 buf_u, u64 count, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (count == 0) return 0;
    if (count > SYSCALL_IO_MAX) return SYSRET_ERR(EINVAL);

    u8 kbuf[SYSCALL_IO_MAX];
    if (copy_from_user(p->space, kbuf, (vaddr_t)buf_u, (size_t)count) < 0)
        return SYSRET_ERR(EFAULT);

    for (size_t i = 0; i < (size_t)count; ++i) {
        console_write_char((char)kbuf[i]);
    }
    return (sysret_t)count;
}

/* ==================================================================== */
/* Dispatch                                                             */
/* ==================================================================== */

typedef sysret_t (*syscall_fn_t)(u64, u64, u64, u64, u64);

static const syscall_fn_t syscall_table[SYSCALL_MAX] = {
    [SYS_PING]          = sys_ping,
    [SYS_EXIT]          = sys_exit,
    [SYS_HANDLE_CREATE] = sys_handle_create,
    [SYS_HANDLE_CLOSE]  = sys_handle_close,
    [SYS_HANDLE_QUERY]  = sys_handle_query,
    [SYS_GETPID]        = sys_getpid,

    [SYS_IPC_CREATE]    = sys_ipc_create,
    [SYS_IPC_SEND]      = sys_ipc_send,
    [SYS_IPC_RECV]      = sys_ipc_recv,
    [SYS_IPC_TRY_SEND]  = sys_ipc_try_send,
    [SYS_IPC_TRY_RECV]  = sys_ipc_try_recv,
    [SYS_IPC_SEND_CAP]  = sys_ipc_send_cap,
    [SYS_IPC_RECV_CAP]  = sys_ipc_recv_cap,
    [SYS_IPC_TRY_RECV_CAP] = sys_ipc_try_recv_cap,
    [SYS_SPAWN]            = sys_spawn,
    [SYS_WAIT]             = sys_wait,
    [SYS_GET_BOOTSTRAP]    = sys_get_bootstrap,
    [SYS_MAP]           = sys_map,
    [SYS_UNMAP]         = sys_unmap,
    [SYS_YIELD]         = sys_yield,

    /* Transitional, remove in 11.5.6 */
    [SYS_KPUTS]         = sys_kputs,
};

sysret_t syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    if (nr >= SYSCALL_MAX || syscall_table[nr] == (syscall_fn_t)0) {
        return SYSRET_ERR(ENOSYS);
    }
    return syscall_table[nr](a0, a1, a2, a3, a4);
}
