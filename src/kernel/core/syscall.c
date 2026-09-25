#include <exyde/syscall.h>
#include <exyde/errno.h>
#include <exyde/handle.h>
#include <exyde/console.h>
#include <exyde/thread.h>
#include <exyde/sched.h>
#include <exyde/process.h>
#include <exyde/fd.h>
#include <exyde/vfs.h>
#include <exyde/uaccess.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
#include <exyde/exec.h>
#include <exyde/channel.h>
#include <exyde/panic.h>

/* One-shot kernel bounce buffer for the transitional SYS_READ / SYS_WRITE.
 * Bigger requests are rejected with -EINVAL. */
#define SYSCALL_IO_MAX  4096u

/* Upper bounds for the microkernel IPC primitive.  Chosen for the
 * first implementation; can be raised later. */
#define IPC_MSG_SIZE_MAX  256u
#define IPC_CAPACITY_MAX  64u

/* Upper bound on a single SYS_MAP request (in 4 KiB pages). */
#define MAP_MAX_PAGES  256u

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
    (void)code; (void)a1; (void)a2; (void)a3; (void)a4;
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

    handle_t h = handle_create(&p->handles, HANDLE_KIND_CHANNEL,
                               HANDLE_RIGHT_READ | HANDLE_RIGHT_WRITE, c);
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

static sysret_t sys_ipc_recv(u64 h, u64 buf_u, u64 max_len,
                             u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    channel_t *c = lookup_channel(p, (handle_t)h, HANDLE_RIGHT_READ);
    if (!c) return SYSRET_ERR(EBADF);
    if (max_len < c->msg_size) return SYSRET_ERR(EINVAL);

    u8 kbuf[IPC_MSG_SIZE_MAX];
    channel_recv(c, kbuf);

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

    u8 kbuf[IPC_MSG_SIZE_MAX];
    if (!channel_try_recv(c, kbuf)) return SYSRET_ERR(EAGAIN);

    if (copy_to_user(p->space, (vaddr_t)buf_u, kbuf, c->msg_size) < 0)
        return SYSRET_ERR(EFAULT);
    return (sysret_t)c->msg_size;
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

/* ==================================================================== */
/* Transitional: VFS / fd / brk (remove in 11.5.6)                      */
/* ==================================================================== */

static sysret_t sys_write(u64 fd, u64 buf_u, u64 count, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);

    file_t *f = fd_get(&p->fds, (int)fd);
    if (!f) return SYSRET_ERR(EBADF);
    if (count == 0) return 0;
    if (count > SYSCALL_IO_MAX) return SYSRET_ERR(EINVAL);

    u8 kbuf[SYSCALL_IO_MAX];
    if (copy_from_user(p->space, kbuf, (vaddr_t)buf_u, (size_t)count) < 0)
        return SYSRET_ERR(EFAULT);

    i64 w = vfs_write(f, kbuf, (size_t)count);
    if (w < 0) return SYSRET_ERR((u64)-w);
    return (sysret_t)w;
}

static sysret_t sys_read(u64 fd, u64 buf_u, u64 count, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);

    file_t *f = fd_get(&p->fds, (int)fd);
    if (!f) return SYSRET_ERR(EBADF);
    if (count == 0) return 0;
    if (count > SYSCALL_IO_MAX) return SYSRET_ERR(EINVAL);

    u8 kbuf[SYSCALL_IO_MAX];
    i64 r = vfs_read(f, kbuf, (size_t)count);
    if (r < 0) return SYSRET_ERR((u64)-r);

    if (copy_to_user(p->space, (vaddr_t)buf_u, kbuf, (size_t)r) < 0)
        return SYSRET_ERR(EFAULT);
    return (sysret_t)r;
}

static sysret_t sys_open(u64 path_u, u64 flags, u64 mode, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);

    char path[VFS_PATH_MAX + 1];
    int sr = strncpy_from_user(p->space, path, (vaddr_t)path_u, sizeof(path));
    if (sr < 0) return SYSRET_ERR((u64)-sr);

    file_t *f = (file_t *)0;
    int r = vfs_open(path, (u32)flags, (u32)mode, &f);
    if (r < 0) return SYSRET_ERR((u64)-r);

    int fd = fd_alloc(&p->fds, f);
    if (fd < 0) {
        vfs_close(f);
        return SYSRET_ERR(EMFILE);
    }
    return (sysret_t)fd;
}

static sysret_t sys_close(u64 fd, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    int r = fd_close(&p->fds, (int)fd);
    if (r < 0) return SYSRET_ERR((u64)-r);
    return 0;
}

static sysret_t sys_lseek(u64 fd, u64 off, u64 whence, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    file_t *f = fd_get(&p->fds, (int)fd);
    if (!f) return SYSRET_ERR(EBADF);
    i64 r = vfs_seek(f, (i64)off, (int)whence);
    if (r < 0) return SYSRET_ERR((u64)-r);
    return (sysret_t)r;
}

static sysret_t sys_brk(u64 new_brk, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);

    if (new_brk == 0) return (sysret_t)p->brk;

    vaddr_t target = (vaddr_t)new_brk;
    vaddr_t cur    = p->brk;

    if (target <= cur) return (sysret_t)cur;
    if (target >= USER_VA_TOP)    return SYSRET_ERR(ENOMEM);
    if (target >  USER_BRK_MAX)   return SYSRET_ERR(ENOMEM);

    vaddr_t cur_page = (cur    + PAGE_SIZE - 1) & ~((vaddr_t)PAGE_SIZE - 1);
    vaddr_t tgt_page = (target + PAGE_SIZE - 1) & ~((vaddr_t)PAGE_SIZE - 1);

    vaddr_t mapped = cur_page;
    for (vaddr_t va = cur_page; va < tgt_page; va += PAGE_SIZE) {
        paddr_t pa = pmm_alloc_page();
        if (!pa) goto fail;
        if (!vmm_map(p->space, va, pa, VM_PRESENT | VM_WRITE | VM_USER)) {
            pmm_free_page(pa);
            goto fail;
        }
        mapped = va + PAGE_SIZE;
    }

    p->brk = target;
    return (sysret_t)target;

fail:
    for (vaddr_t va = cur_page; va < mapped; va += PAGE_SIZE) {
        paddr_t pa = 0;
        if (vmm_query(p->space, va, &pa, (u32 *)0)) {
            vmm_unmap(p->space, va);
            pmm_free_page(pa);
        }
    }
    return SYSRET_ERR(ENOMEM);
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
    [SYS_MAP]           = sys_map,
    [SYS_UNMAP]         = sys_unmap,
    [SYS_YIELD]         = sys_yield,

    /* Transitional, remove in 11.5.6 */
    [SYS_WRITE]         = sys_write,
    [SYS_READ]          = sys_read,
    [SYS_OPEN]          = sys_open,
    [SYS_CLOSE]         = sys_close,
    [SYS_LSEEK]         = sys_lseek,
    [SYS_BRK]           = sys_brk,
};

sysret_t syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    if (nr >= SYSCALL_MAX || syscall_table[nr] == (syscall_fn_t)0) {
        return SYSRET_ERR(ENOSYS);
    }
    return syscall_table[nr](a0, a1, a2, a3, a4);
}
