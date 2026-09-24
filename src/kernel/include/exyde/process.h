#ifndef EXYDE_PROCESS_H
#define EXYDE_PROCESS_H

#include <exyde/types.h>
#include <exyde/vmm.h>
#include <exyde/elf.h>
#include <exyde/thread.h>
#include <exyde/handle.h>
#include <exyde/fd.h>

typedef struct process {
    u64            pid;
    const char    *name;
    vmm_space_t    space;
    elf_image_t    image;
    thread_t      *main_thread;
    handle_table_t handles;
    fd_table_t     fds;
    u64            initial_rsp;  /* set by process_spawn; 0 otherwise */
    vaddr_t        brk;          /* program break: first free user VA */
} process_t;

process_t *process_create_from_elf(const char *name,
                                   const void *elf, size_t size);
void       process_destroy(process_t *p);

#endif /* EXYDE_PROCESS_H */
