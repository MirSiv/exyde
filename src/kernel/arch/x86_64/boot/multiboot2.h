#ifndef EXYDE_ARCH_X86_64_MULTIBOOT2_H
#define EXYDE_ARCH_X86_64_MULTIBOOT2_H

#include <exyde/types.h>

#define MB2_BOOTLOADER_MAGIC 0x36D76289u

typedef struct {
    u32 total_size;
    u32 reserved;
} mb2_info_header_t;

typedef struct {
    u32 type;
    u32 size;
} mb2_tag_t;

#define MB2_TAG_END   0u
#define MB2_TAG_MMAP  6u

typedef struct {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
} mb2_tag_mmap_t;

typedef struct {
    u64 addr;
    u64 len;
    u32 type;
    u32 reserved;
} mb2_mmap_entry_t;

#endif /* EXYDE_ARCH_X86_64_MULTIBOOT2_H */
