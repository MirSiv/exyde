#include <exyde/elf_table.h>

/* Symbols produced by objcopy.  The kernel Makefile embeds every
 * userspace ELF under build/NAME.elf and objcopy mangles the path
 * into _binary_build_NAME_elf_{start,end}. */
extern const u8 _binary_build_init_elf_start[];
extern const u8 _binary_build_init_elf_end[];
extern const u8 _binary_build_test_elf_start[];
extern const u8 _binary_build_test_elf_end[];
extern const u8 _binary_build_echo_elf_start[];
extern const u8 _binary_build_echo_elf_end[];
extern const u8 _binary_build_exy_vfs_elf_start[];
extern const u8 _binary_build_exy_vfs_elf_end[];

static const elf_entry_t table[] = {
    { "init", _binary_build_init_elf_start, _binary_build_init_elf_end },
    { "test", _binary_build_test_elf_start, _binary_build_test_elf_end },
    { "echo", _binary_build_echo_elf_start, _binary_build_echo_elf_end },
    { "exy-vfs", _binary_build_exy_vfs_elf_start, _binary_build_exy_vfs_elf_end },
};

#define ELF_TABLE_N ((u32)(sizeof(table) / sizeof(table[0])))

u32 elf_table_count(void) {
    return ELF_TABLE_N;
}

static bool name_eq(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

const elf_entry_t *elf_table_lookup(const char *name) {
    if (!name) return (const elf_entry_t *)0;
    for (u32 i = 0; i < ELF_TABLE_N; ++i) {
        if (name_eq(table[i].name, name)) return &table[i];
    }
    return (const elf_entry_t *)0;
}

const elf_entry_t *elf_table_get(u32 index) {
    if (index >= ELF_TABLE_N) return (const elf_entry_t *)0;
    return &table[index];
}
