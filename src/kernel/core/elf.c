#include <exyde/elf.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
#include <exyde/thread.h>
#include <exyde/sched.h>
#include <exyde/uaccess.h>
#include <exyde/panic.h>

#define EI_NIDENT   16
#define EI_CLASS    4
#define EI_DATA     5

#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define EM_X86_64   0x3E

#define PT_LOAD     1

typedef struct {
    u8  e_ident[EI_NIDENT];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} elf64_phdr_t;

_Static_assert(sizeof(elf64_ehdr_t) == 64, "elf64_ehdr_t");
_Static_assert(sizeof(elf64_phdr_t) == 56, "elf64_phdr_t");

bool elf_load(vmm_space_t space, const void *data, size_t size,
              elf_image_t *out) {
    if (!space || !data || !out) return false;
    if (size < sizeof(elf64_ehdr_t)) return false;

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)data;

    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') return false;
    if (eh->e_ident[EI_CLASS] != ELFCLASS64) return false;
    if (eh->e_ident[EI_DATA]  != ELFDATA2LSB) return false;
    if (eh->e_type    != ET_EXEC)   return false;
    if (eh->e_machine != EM_X86_64) return false;
    if (eh->e_phentsize != sizeof(elf64_phdr_t)) return false;
    if (eh->e_phnum == 0) return false;

    if (eh->e_phoff > size) return false;
    u64 ph_bytes = (u64)eh->e_phnum * sizeof(elf64_phdr_t);
    if (ph_bytes / sizeof(elf64_phdr_t) != eh->e_phnum) return false;
    if (ph_bytes > size - eh->e_phoff) return false;

    if (eh->e_entry < USER_VA_BASE) return false;

    /* copy_to_user dereferences user VAs in the current address space,
     * so switch CR3 to the target for the duration of the load. */
    vmm_space_t prev = thread_current()->space;
    bool need_switch = (prev != space);
    if (need_switch) vmm_switch(space);

    vaddr_t lowest  = (vaddr_t)-1;
    vaddr_t highest = 0;
    int     loaded  = 0;
    bool    ok      = false;

    const elf64_phdr_t *ph =
        (const elf64_phdr_t *)((const u8 *)data + eh->e_phoff);

    static const u8 zeros[PAGE_SIZE] = {0};

    for (u32 i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_memsz == 0)      continue;

        if (ph[i].p_filesz > ph[i].p_memsz) return false;
        if (ph[i].p_offset > size) return false;
        if (ph[i].p_filesz > size - ph[i].p_offset) return false;

        if (ph[i].p_vaddr < USER_VA_BASE) return false;
        if (ph[i].p_memsz > (u64)-1 - ph[i].p_vaddr) return false;

        vaddr_t seg_start = ph[i].p_vaddr & ~((vaddr_t)PAGE_SIZE - 1);
        vaddr_t seg_end   = (ph[i].p_vaddr + ph[i].p_memsz
                             + PAGE_SIZE - 1) & ~((vaddr_t)PAGE_SIZE - 1);
        if (seg_end < seg_start) return false;
        if (seg_end > 0x0000800000000000ULL) return false;

        for (vaddr_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            paddr_t pa = pmm_alloc_page();
            if (pa == 0) goto done;
            if (!vmm_map(space, va, pa, VM_PRESENT | VM_WRITE | VM_USER)) {
                pmm_free_page(pa);
                goto done;
            }
        }

        /* Zero exactly [p_vaddr, p_vaddr + p_memsz), then overlay the
         * file bytes at [p_vaddr, p_vaddr + p_filesz).  This touches
         * only this segment's own bytes -- page padding and any bytes
         * of a neighbouring segment sharing the same page are left
         * alone.  BSS (the tail beyond p_filesz) stays zero. */
        vaddr_t dst = (vaddr_t)ph[i].p_vaddr;
        size_t  rem = (size_t)ph[i].p_memsz;
        while (rem > 0) {
            size_t chunk = rem > PAGE_SIZE ? PAGE_SIZE : rem;
            if (copy_to_user(space, dst, zeros, chunk) < 0) goto done;
            dst += chunk;
            rem -= chunk;
        }
        if (ph[i].p_filesz > 0) {
            if (copy_to_user(space, (vaddr_t)ph[i].p_vaddr,
                             (const u8 *)data + ph[i].p_offset,
                             (size_t)ph[i].p_filesz) < 0) goto done;
        }

        if (seg_start < lowest)  lowest  = seg_start;
        if (seg_end   > highest) highest = seg_end;
        loaded++;
    }

    if (loaded == 0) goto done;

    out->entry     = (vaddr_t)eh->e_entry;
    out->load_base = lowest;
    out->load_size = (size_t)(highest - lowest);
    ok = true;

done:
    if (need_switch) vmm_switch(prev);
    return ok;
}
