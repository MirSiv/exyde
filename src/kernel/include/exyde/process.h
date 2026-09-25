#ifndef EXYDE_PROCESS_H
#define EXYDE_PROCESS_H

#include <exyde/types.h>
#include <exyde/vmm.h>
#include <exyde/elf.h>
#include <exyde/thread.h>
#include <exyde/handle.h>
#include <exyde/fd.h>
#include <exyde/waitq.h>

/* Kernel-side process object.
 *
 * Phase 11.5.0 note: `image`, `fds`, and `brk` are transitional fields
 * -- they belong to the monolithic era and will be removed as VFS and
 * POSIX move to userspace services (see EXYDE_PHASES.md, Phase 11.5). */
typedef struct process {
    u64            pid;
    const char    *name;
    vmm_space_t    space;
    elf_image_t    image;
    thread_t      *main_thread;
    handle_table_t handles;
    fd_table_t     fds;
    u64            initial_rsp;
    vaddr_t        brk;          /* transitional */
    vaddr_t        next_map_va;  /* hint for SYS_MAP with hint == 0 */

    /* Phase 11.5.2: lifecycle.
     *
     * refcount is 1 at creation.  It is transferred to the main
     * thread (main_thread != NULL) or kept by the creator (main_thread
     * == NULL).  Whoever owns the ref must release it via
     * process_unref; the last release destroys the process.
     *
     * exit_code / exited are set by process_exit, which is called
     * from SYS_EXIT before the thread tears down.  A waiter blocked
     * in SYS_WAIT wakes on the waiters waitq, reads exit_code, and
     * closes its handle (dropping the last ref). */
    u32            refcount;
    i32            exit_code;
    u8             exited;
    u8             _pad[3];
    handle_t       bootstrap_handle;
    waitq_t        waiters;
} process_t;

process_t *process_create_from_elf(const char *name,
                                   const void *elf, size_t size);

/* Release a reference.  When refcount reaches 0, destroys the
 * process (frees handles, fd table, address space, struct).  Must be
 * called with IRQs disabled or in a context where no concurrent
 * access to the process is possible. */
void process_unref(process_t *p);

/* Take an extra reference.  Used when a handle on the process is
 * created, so the process survives after its main thread exits until
 * the handle is closed. */
void process_ref(process_t *p);

/* Record the process's exit status and wake all waiters.  Idempotent:
 * a second call is a no-op.  Does NOT drop any reference; the caller
 * (free_thread) handles refcounting separately. */
void process_exit(process_t *p, int code);

/* Direct destroy.  Only for internal failure paths in this file,
 * before a thread has been attached.  External code MUST go through
 * process_unref. */
void process_destroy(process_t *p);

#endif /* EXYDE_PROCESS_H */
