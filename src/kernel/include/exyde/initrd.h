#ifndef EXYDE_INITRD_H
#define EXYDE_INITRD_H

#include <exyde/types.h>

/* The cpio newc initrd is embedded at link time by objcopy; see
 * src/kernel/Makefile.  As with init_elf.h, objcopy derives the
 * symbol names from the input path relative to the current directory
 * at invocation time, so we always invoke it from the project root
 * and feed it `build/initrd`, yielding:
 *
 *     _binary_build_initrd_start
 *     _binary_build_initrd_end
 *
 * The kernel does not parse this image.  It is mapped read-only into
 * the bootstrap process's address space (see exec.h::INITRD_VA) and
 * init reads it there. */

extern const u8 _binary_build_initrd_start[];
extern const u8 _binary_build_initrd_end[];

#define exyde_initrd      ((const u8 *)_binary_build_initrd_start)
#define exyde_initrd_size ((u64)(_binary_build_initrd_end - _binary_build_initrd_start))

#endif /* EXYDE_INITRD_H */
