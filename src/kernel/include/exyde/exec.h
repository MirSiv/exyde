#ifndef EXYDE_EXEC_H
#define EXYDE_EXEC_H

#include <exyde/types.h>
#include <exyde/process.h>

#define EXEC_MAX_ARGS    16
#define EXEC_MAX_ENVS    16
#define EXEC_STACK_PAGES 4   /* 16 KiB */

/* Build the initial user stack for a SysV-style process entry:
 *   RSP -> [argc][argv[0]..argv[argc-1]][NULL][envp[0]..envp[envc-1]][NULL]
 * with the string bodies above the pointer arrays.  RSP is 16-byte
 * aligned.  Returns 0 on failure, otherwise the initial RSP. */
u64 exec_build_initial_stack(vmm_space_t space, vaddr_t stack_top,
                             int argc, const char *const *argv,
                             int envc, const char *const *envp);

/* Spawn a new user process from an in-memory ELF image.  Sets up
 * fd 0/1/2 (console), maps the user stack, builds argc/argv/envp,
 * and creates its main thread in ring 3.  Returns the process, or
 * NULL on failure. */
process_t *process_spawn(const char *name,
                         const void *elf, size_t elf_size,
                         int argc, const char *const *argv,
                         int envc, const char *const *envp);

#endif /* EXYDE_EXEC_H */
