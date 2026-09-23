#ifndef EXYDE_INIT_ELF_H
#define EXYDE_INIT_ELF_H

#include <exyde/types.h>

/* The init ELF image is embedded at link time by objcopy; see
 * src/kernel/Makefile.  objcopy derives the symbol names from the
 * input path relative to the current directory at invocation time,
 * so we always invoke it from the project root and feed it
 * `build/init.elf`, yielding:
 *
 *     _binary_build_init_elf_start
 *     _binary_build_init_elf_end
 *
 * The old hand-assembled blob (core/init_elf.c) is gone. */
extern const u8 _binary_build_init_elf_start[];
extern const u8 _binary_build_init_elf_end[];

#define exyde_init_elf      ((const u8 *)_binary_build_init_elf_start)
#define exyde_init_elf_size ((u64)(_binary_build_init_elf_end - _binary_build_init_elf_start))

#endif /* EXYDE_INIT_ELF_H */
