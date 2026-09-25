#ifndef EXYDE_PROCESS_H
#define EXYDE_PROCESS_H

#include <exyde/types.h>
#include <exyde/vmm.h>
#include <exyde/thread.h>
#include <exyde/handle.h>
#include <exyde/waitq.h>

/* Kernel-side process object, Phase 11.5.6.
 *
 * The monolithic-era fields image, fds, brk are gone.  The kernel
 * knows only about: an address space, an entry point, a main thread,
 * a capability table, a bootstrap handle, and lifecycle state.  The
 * userspace ELF loader will land in 11.5.7; for now process_spawn
 * still calls elf_load to build the address space for the bootstrap
 * init.elf. */

typedef struct process {
    u64            pid;
    const char    *name;
    vmm_space_t    space;
    vaddr_t        entry;        /* user entry point */
    thread_t      *main_thread;
    handle_table_t handles;
    u64            initial_rsp;
    vaddr_t        next_map_va;  /* hint for SYS_MAP with hint == 0 */

    u32            refcount;
    i32            exit_code;
    u8             exited;
    u8             _pad[3];
    handle_t       bootstrap_handle;
    waitq_t        waiters;
} process_t;

process_t *process_create_from_elf(const char *name,
                                   const void *elf, size_t size);
void process_unref(process_t *p);
void process_ref(process_t *p);
void process_exit(process_t *p, int code);
void process_destroy(process_t *p);

#endif /* EXYDE_PROCESS_H */
