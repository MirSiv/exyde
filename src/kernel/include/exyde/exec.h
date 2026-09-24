#ifndef EXYDE_EXEC_H
#define EXYDE_EXEC_H

#include <exyde/types.h>
#include <exyde/process.h>

#define EXEC_MAX_ARGS    16
#define EXEC_MAX_ENVS    16
#define EXEC_STACK_PAGES 4   /* 16 KiB */

/* Two distinct user stacks:
 *
 *   USER_STACK_TOP       -- low, used by the ring3 self-test in
 *                           kmain.c.  Kept low so the ring3 test's
 *                           expected page delta on destroy does not
 *                           depend on heap layout.
 *
 *   USER_STACK_TOP_INIT  -- high, used by process_spawn() for the
 *                           first real user process.  Higher so the
 *                           process has room for brk/malloc between
 *                           the ELF image and its stack.
 *
 * USER_BRK_MAX is clamped against the LOW stack (plus one guard
 * page) so that a run-away brk can never collide with either stack. */
#define USER_STACK_TOP       (USER_VA_BASE + 0x00100000ULL)  /*  1 MiB */
#define USER_STACK_TOP_INIT  (USER_VA_BASE + 0x01000000ULL)  /* 16 MiB */
#define USER_BRK_MAX         (USER_STACK_TOP - (EXEC_STACK_PAGES + 1) * PAGE_SIZE)

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
