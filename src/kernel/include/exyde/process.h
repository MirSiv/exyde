#ifndef EXYDE_PROCESS_H
#define EXYDE_PROCESS_H

#include <exyde/types.h>
#include <exyde/vmm.h>
#include <exyde/elf.h>
#include <exyde/thread.h>
#include <exyde/handle.h>
#include <exyde/fd.h>

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
} process_t;

process_t *process_create_from_elf(const char *name,
                                   const void *elf, size_t size);
void       process_destroy(process_t *p);

#endif /* EXYDE_PROCESS_H */
