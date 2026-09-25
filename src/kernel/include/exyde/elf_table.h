#ifndef EXYDE_ELF_TABLE_H
#define EXYDE_ELF_TABLE_H

#include <exyde/types.h>

/* Table of ELF images embedded into the kernel at link time (via
 * objcopy -I binary, see src/kernel/Makefile).  Phase 11.5.2 uses
 * this as the source of programs for SYS_SPAWN, and the bootstrap
 * path (kmain) uses it to load the initial service manager.
 *
 * This is intentionally a stop-gap.  The proper solution -- loading
 * files from an initrd or a userspace filesystem server -- arrives
 * in Phase 11.5.3. */

typedef struct {
    const char *name;
    const u8   *blob_start;
    const u8   *blob_end;
} elf_entry_t;

/* Blob size in bytes (end - start). */
static inline u64 elf_entry_size(const elf_entry_t *e) {
    return (u64)(e->blob_end - e->blob_start);
}

u32 elf_table_count(void);
const elf_entry_t *elf_table_lookup(const char *name);
const elf_entry_t *elf_table_get(u32 index);

#endif /* EXYDE_ELF_TABLE_H */
