#include <exyde/elf.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
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

/* Zero [pa, pa+n) via the bootstrap identity map (PMM pages always
 * lie below 1 GiB). */
static void zero_phys_range(paddr_t pa, size_t n) {
    u8 *p = (u8 *)(uintptr_t)pa;
    for (size_t i = 0; i < n; ++i) p[i] = 0;
}

/* Copy one byte to user VA.  Caller guarantees va was mapped by us. */
static bool poke_user_byte(vmm_space_t space, vaddr_t va, u8 v) {
    paddr_t pa = 0;
    if (!vmm_query(space, va & ~((vaddr_t)PAGE_SIZE - 1), &pa, (u32 *)0))
        return false;
    u8 *dst = (u8 *)(uintptr_t)pa + (va & (PAGE_SIZE - 1));
    *dst = v;
    return true;
}

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

    /* Program header table must lie fully inside the file. */
    if (eh->e_phoff > size) return false;
    u64 ph_bytes = (u64)eh->e_phnum * sizeof(elf64_phdr_t);
    if (ph_bytes / sizeof(elf64_phdr_t) != eh->e_phnum) return false; /* overflow */
    if (ph_bytes > size - eh->e_phoff) return false;

    /* Entry point must be inside user space. */
    if (eh->e_entry < USER_VA_BASE) return false;

    vaddr_t lowest  = (vaddr_t)-1;
    vaddr_t highest = 0;
    int     loaded  = 0;

    const elf64_phdr_t *ph =
        (const elf64_phdr_t *)((const u8 *)data + eh->e_phoff);

    for (u32 i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_memsz == 0)      continue;

        /* File-side bounds. */
        if (ph[i].p_filesz > ph[i].p_memsz) return false;
        if (ph[i].p_offset > size) return false;
        if (ph[i].p_filesz > size - ph[i].p_offset) return false;

        /* Address-side bounds: entire segment in user space, no wrap. */
        if (ph[i].p_vaddr < USER_VA_BASE) return false;
        if (ph[i].p_memsz > (u64)-1 - ph[i].p_vaddr) return false;

        vaddr_t seg_start = ph[i].p_vaddr & ~((vaddr_t)PAGE_SIZE - 1);
        vaddr_t seg_end   = (ph[i].p_vaddr + ph[i].p_memsz
                             + PAGE_SIZE - 1) & ~((vaddr_t)PAGE_SIZE - 1);
        if (seg_end < seg_start) return false;

        /* Reject VA range that crosses into the kernel half.  On
         * x86_64, canonical user VA is < 0x0000800000000000; we
         * enforce that here so a malformed ELF can't ask us to map
         * over the kernel's PML4[0]. */
        if (seg_end > 0x0000800000000000ULL) return false;

        /* Map the pages. */
        for (vaddr_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            paddr_t pa = pmm_alloc_page();
            if (pa == 0) return false;
            zero_phys_range(pa, PAGE_SIZE);
            if (!vmm_map(space, va, pa, VM_PRESENT | VM_WRITE | VM_USER)) {
                pmm_free_page(pa);
                return false;
            }
        }

        /* Copy file bytes.  BSS tail of the last page is already
         * zero because we zeroed the whole page. */
        for (u64 off = 0; off < ph[i].p_filesz; ++off) {
            vaddr_t va = ph[i].p_vaddr + off;
            u8      b  = ((const u8 *)data)[ph[i].p_offset + off];
            if (!poke_user_byte(space, va, b)) return false;
        }

        if (seg_start < lowest)  lowest  = seg_start;
        if (seg_end   > highest) highest = seg_end;
        loaded++;
    }

    if (loaded == 0) return false;

    out->entry     = (vaddr_t)eh->e_entry;
    out->load_base = lowest;
    out->load_size = (size_t)(highest - lowest);
    return true;
}
