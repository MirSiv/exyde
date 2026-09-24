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
#include <exyde/exec.h>
#include <exyde/panic.h>

/* One-shot kernel bounce buffer for SYS_READ / SYS_WRITE.  Bigger
 * requests are rejected with -EINVAL — a proper streaming design will
 * come with a real FS that can hand out page-aligned I/O. */
#define SYSCALL_IO_MAX  4096u

static process_t *current_process(void) {
    thread_t *t = thread_current();
    if (!t || !t->process) return (process_t *)0;
    return (process_t *)t->process;
}

/* ---- handles (unchanged) ------------------------------------------- */

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

/* ---- fd syscalls --------------------------------------------------- */

static sysret_t sys_write(u64 fd, u64 buf_u, u64 count,
                          u64 a3, u64 a4) {
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

static sysret_t sys_read(u64 fd, u64 buf_u, u64 count,
                         u64 a3, u64 a4) {
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

static sysret_t sys_open(u64 path_u, u64 flags, u64 mode,
                         u64 a3, u64 a4) {
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

static sysret_t sys_lseek(u64 fd, u64 off, u64 whence,
                          u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    file_t *f = fd_get(&p->fds, (int)fd);
    if (!f) return SYSRET_ERR(EBADF);
    i64 r = vfs_seek(f, (i64)off, (int)whence);
    if (r < 0) return SYSRET_ERR((u64)-r);
    return (sysret_t)r;
}

static sysret_t sys_getpid(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    return (sysret_t)p->pid;
}

static sysret_t sys_brk(u64 new_brk, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);

    /* Query form: brk(0) returns the current program break. */
    if (new_brk == 0) return (sysret_t)p->brk;

    vaddr_t target = (vaddr_t)new_brk;
    vaddr_t cur    = p->brk;

    /* Shrinking is not supported in 11.0.3.  Return the current
     * value so the caller can detect that nothing changed. */
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
    /* Roll back everything mapped by this call. */
    for (vaddr_t va = cur_page; va < mapped; va += PAGE_SIZE) {
        paddr_t pa = 0;
        if (vmm_query(p->space, va, &pa, (u32 *)0)) {
            vmm_unmap(p->space, va);
            pmm_free_page(pa);
        }
    }
    return SYSRET_ERR(ENOMEM);
}

/* ---- dispatch ------------------------------------------------------ */

typedef sysret_t (*syscall_fn_t)(u64, u64, u64, u64, u64);

static const syscall_fn_t syscall_table[SYSCALL_MAX] = {
    [SYS_PING]          = sys_ping,
    [SYS_WRITE]         = sys_write,
    [SYS_EXIT]          = sys_exit,
    [SYS_HANDLE_CREATE] = sys_handle_create,
    [SYS_HANDLE_CLOSE]  = sys_handle_close,
    [SYS_HANDLE_QUERY]  = sys_handle_query,
    [SYS_READ]          = sys_read,
    [SYS_OPEN]          = sys_open,
    [SYS_CLOSE]         = sys_close,
    [SYS_LSEEK]         = sys_lseek,
    [SYS_GETPID]        = sys_getpid,
    [SYS_BRK]           = sys_brk,
};

sysret_t syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    if (nr >= SYSCALL_MAX || syscall_table[nr] == (syscall_fn_t)0) {
        return SYSRET_ERR(ENOSYS);
    }
    return syscall_table[nr](a0, a1, a2, a3, a4);
}
