#include <exyde/types.h>
#include <exyde/user.h>
#include <arch/x86_64/gdt.h>

struct tss64 {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

/* Runtime GDT:
 *   0x00  null
 *   0x08  kernel code (64-bit, DPL=0)
 *   0x10  kernel data (DPL=0)
 *   0x18  user data   (DPL=3)  -> selector 0x1B
 *   0x20  user code   (64-bit, DPL=3) -> selector 0x23
 *   0x28  TSS low  \
 *   0x30  TSS high / (16 bytes total)
 *
 * The user data/code order matters for SYSRET: it computes
 *   CS = STAR[63:48] + 16
 *   SS = STAR[63:48] + 8
 * and this layout lets STAR[63:48] = 0x10 land on
 * user code = 0x20 and user data = 0x18. */
static u64 gdt[7];

static struct tss64 tss;

static u8 ist_stack_df[8192]  __attribute__((aligned(16)));
static u8 ist_stack_nmi[8192] __attribute__((aligned(16)));
static u8 ist_stack_mc[8192]  __attribute__((aligned(16)));

extern void gdt_flush(struct gdt_ptr *ptr);

static u64 make_flat_gdt_entry(u32 base, u32 limit, u8 access, u8 flags) {
    return (u64)(limit & 0xFFFFu)
         | ((u64)(base & 0xFFFFu) << 16)
         | ((u64)((base >> 16) & 0xFFu) << 32)
         | ((u64)access << 40)
         | ((u64)((limit >> 16) & 0xFu) << 48)
         | ((u64)(flags & 0xFu) << 52)
         | ((u64)((base >> 24) & 0xFFu) << 56);
}

static void tss_init(void) {
    u8 *raw = (u8 *)&tss;
    for (u32 i = 0; i < sizeof(tss); ++i) raw[i] = 0;

    tss.rsp0 = 0;
    tss.ist[0] = (u64)(ist_stack_df  + sizeof(ist_stack_df));
    tss.ist[1] = (u64)(ist_stack_nmi + sizeof(ist_stack_nmi));
    tss.ist[2] = (u64)(ist_stack_mc  + sizeof(ist_stack_mc));
    tss.iomap_base = (u16)sizeof(tss);
}

static void gdt_set_entries(void) {
    gdt[0] = 0;

    /* 0x08: kernel code. */
    gdt[1] = make_flat_gdt_entry(0, 0xFFFFFu, 0x9Au, 0xAu);

    /* 0x10: kernel data. */
    gdt[2] = make_flat_gdt_entry(0, 0xFFFFFu, 0x92u, 0xCu);

    /* 0x18: user data, DPL=3. */
    gdt[3] = make_flat_gdt_entry(0, 0xFFFFFu, 0xF2u, 0xCu);

    /* 0x20: user code, DPL=3. */
    gdt[4] = make_flat_gdt_entry(0, 0xFFFFFu, 0xFAu, 0xAu);

    /* 0x28 + 0x30: TSS. */
    u64 base  = (u64)&tss;
    u32 limit = (u32)(sizeof(tss) - 1);

    gdt[5] = (u64)(limit & 0xFFFFu)
           | ((base & 0xFFFFu) << 16)
           | (((base >> 16) & 0xFFu) << 32)
           | ((u64)0x89u << 40)
           | ((u64)((limit >> 16) & 0xFu) << 48)
           | ((u64)0u << 52)
           | (((base >> 24) & 0xFFu) << 56);

    gdt[6] = (base >> 32) & 0xFFFFFFFFu;
}

void gdt_set_kernel_stack(u64 rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    tss_init();
    gdt_set_entries();

    struct gdt_ptr ptr = {
        .limit = (u16)(sizeof(gdt) - 1),
        .base  = (u64)&gdt[0],
    };
    gdt_flush(&ptr);
}
