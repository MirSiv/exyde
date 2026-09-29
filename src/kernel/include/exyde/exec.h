#ifndef EXYDE_EXEC_H
#define EXYDE_EXEC_H

#include <exyde/types.h>
#include <exyde/process.h>

#define EXEC_MAX_ARGS    16
#define EXEC_MAX_ENVS    16
#define EXEC_STACK_PAGES 4   /* 16 KiB */

/* User-space VA layout, in increasing order:
 *
 *   USER_VA_BASE + 0x00000000   ELF image (load_base .. load_base+size)
 *                               then brk / heap / SYS_MAP allocations
 *   USER_VA_BASE + 0x00100000   USER_STACK_TOP      (ring3 self-test)
 *   USER_VA_BASE + 0x01000000   USER_STACK_TOP_INIT (init and children)
 *   USER_VA_BASE + 0x02000000   INITRD_VA           (read-only initrd)
 *
 * USER_BRK_MAX is clamped against the LOW stack (plus one guard
 * page) so that a runaway brk can never collide with either stack.
 * INITRD_VA sits well above the high stack so nothing in normal
 * process operation can accidentally land on top of it. */
#define USER_STACK_TOP       (USER_VA_BASE + 0x00100000ULL)  /*  1 MiB */
#define USER_STACK_TOP_INIT  (USER_VA_BASE + 0x01000000ULL)  /* 16 MiB */
#define USER_BRK_MAX         (USER_STACK_TOP - (EXEC_STACK_PAGES + 1) * PAGE_SIZE)
#define INITRD_VA            (USER_VA_BASE + 0x02000000ULL)  /* 32 MiB */

/* Build the initial user stack for a SysV-style process entry:
 *   RSP -> [argc][argv[0]..argv[argc-1]][NULL][envp[0]..envp[envc-1]][NULL]
 * with the string bodies above the pointer arrays.  RSP is 16-byte
 * aligned.  Returns 0 on failure, otherwise the initial RSP. */
u64 exec_build_initial_stack(vmm_space_t space, vaddr_t stack_top,
                             int argc, const char *const *argv,
                             int envc, const char *const *envp);

/* Spawn a new user process from an in-memory ELF image.  Maps the
 * user stack, builds argc/argv/envp, and creates its main thread in
 * ring 3.  Returns the process, or NULL on failure. */
process_t *process_spawn(const char *name,
                         const void *elf, size_t elf_size,
                         int argc, const char *const *argv,
                         int envc, const char *const *envp);

/* Same as process_spawn(), plus a read-only initrd mapped at
 * INITRD_VA.  Used only for the bootstrap init; ordinary processes
 * do not get an initrd.  `initrd` may be NULL, in which case this
 * is identical to process_spawn(). */
process_t *process_spawn_init(const char *name,
                              const void *elf, size_t elf_size,
                              const void *initrd, size_t initrd_size,
                              int argc, const char *const *argv,
                              int envc, const char *const *envp);

#endif /* EXYDE_EXEC_H */
