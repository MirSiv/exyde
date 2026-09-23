#ifndef EXYDE_ELF_H
#define EXYDE_ELF_H

#include <exyde/types.h>
#include <exyde/vmm.h>

/* Result of loading an ELF image into an address space. */
typedef struct {
    vaddr_t entry;      /* ELF entry point (virtual)              */
    vaddr_t load_base;  /* lowest mapped page, page-aligned       */
    size_t  load_size;  /* span from load_base to highest mapped  */
} elf_image_t;

/* Parse an ELF64 image from `data` (size bytes) and map all PT_LOAD
 * segments into `space`.  File bytes are copied, BSS is zeroed.
 * Returns true on success.  On failure, pages already mapped by this
 * call are leaked — caller is expected to vmm_destroy(space). */
bool elf_load(vmm_space_t space, const void *data, size_t size,
              elf_image_t *out);

#endif /* EXYDE_ELF_H */
