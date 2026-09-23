#include <exyde/bootinfo.h>
#include <exyde/memmap.h>
#include <exyde/console.h>
#include <exyde/panic.h>
#include "multiboot2.h"

static inline const mb2_tag_t *next_tag(const mb2_tag_t *tag) {
    u64 addr = (u64)tag + (u64)tag->size;
    addr = (addr + 7u) & ~(u64)7u;
    return (const mb2_tag_t *)addr;
}

void bootinfo_init(u64 raw_addr) {
    if (raw_addr == 0) {
        panic("bootinfo: null multiboot2 info");
    }

    const mb2_info_header_t *hdr = (const mb2_info_header_t *)raw_addr;
    if (hdr->total_size < sizeof(mb2_info_header_t)) {
        panic("bootinfo: malformed multiboot2 info");
    }

    memmap_reset();
    /* Reserve the info structure itself, in case the bootloader did not. */
    (void)memmap_reserve(raw_addr, hdr->total_size);

    const u8 *base = (const u8 *)raw_addr;
    const u8 *end  = base + hdr->total_size;
    const mb2_tag_t *tag = (const mb2_tag_t *)(base + sizeof(mb2_info_header_t));

    bool found_mmap = false;

    while ((const u8 *)tag + sizeof(mb2_tag_t) <= end) {
        if (tag->type == MB2_TAG_END) break;

        if (tag->type == MB2_TAG_MMAP) {
            const mb2_tag_mmap_t *m = (const mb2_tag_mmap_t *)tag;
            if (m->entry_size < sizeof(mb2_mmap_entry_t)) {
                panic("bootinfo: bad mmap entry size");
            }
            const u8 *entries     = (const u8 *)m + sizeof(mb2_tag_mmap_t);
            const u8 *entries_end = (const u8 *)m + m->size;

            for (const u8 *p = entries; p + m->entry_size <= entries_end;
                 p += m->entry_size) {
                const mb2_mmap_entry_t *e = (const mb2_mmap_entry_t *)p;
                if (e->len == 0) continue;
                if (!memmap_add(e->addr, e->len, e->type)) {
                    panic("bootinfo: memmap overflow");
                }
            }
            found_mmap = true;
        }

        tag = next_tag(tag);
    }

    if (!found_mmap) {
        panic("bootinfo: no mmap tag in multiboot2 info");
    }

    console_write("memmap: ");
    console_write_hex((u64)memmap_count());
    console_write(" regions\n");
}
