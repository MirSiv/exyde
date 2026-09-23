#include <exyde/types.h>
#include <exyde/syscall.h>
#include <exyde/user.h>
#include <arch/x86_64/gdt.h>

/* MSR indices. */
#define MSR_EFER           0xC0000080u
#define MSR_STAR           0xC0000081u
#define MSR_LSTAR          0xC0000082u
#define MSR_FMASK          0xC0000084u
#define MSR_GS_BASE        0xC0000101u
#define MSR_KERNEL_GS_BASE 0xC0000102u

#define EFER_SCE           (1u << 0)

/* Cleared in RFLAGS on syscall entry: TF, IF, DF, NT, AC. */
#define SYSCALL_FMASK      ((u64)0x44700u)

/* GS-relative syscall state.  Layout MUST match syscall.asm. */
typedef struct {
    u64 kernel_rsp;   /* [gs:0] */
    u64 scratch;      /* [gs:8] */
} syscall_state_t;

static syscall_state_t syscall_state;

extern void syscall_entry(void);

static inline u64 rdmsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static inline void wrmsr(u32 msr, u64 v) {
    __asm__ volatile("wrmsr" :: "c"(msr),
                     "a"((u32)(v & 0xFFFFFFFFu)),
                     "d"((u32)(v >> 32)));
}

void syscall_arch_init(void) {
    syscall_state.kernel_rsp = 0;
    syscall_state.scratch    = 0;

    /* Enable SYSCALL/SYSRET. */
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);

    /* STAR:
     *   [47:32] = 0x08 (SYSCALL CS; SS = CS+8 = 0x10)
     *   [63:48] = 0x10 (SYSRET base; CS = base+16 = 0x20,
     *                                SS = base+8  = 0x18)  */
    u64 star = ((u64)0x08u << 32) | ((u64)0x10u << 48);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (u64)(uintptr_t)&syscall_entry);
    wrmsr(MSR_FMASK, SYSCALL_FMASK);

    /* GS bases:  MSR_GS_BASE = user GS (0), MSR_KERNEL_GS_BASE = state. */
    wrmsr(MSR_GS_BASE,        0);
    wrmsr(MSR_KERNEL_GS_BASE, (u64)(uintptr_t)&syscall_state);
}

void arch_set_kernel_stack(vaddr_t rsp0) {
    gdt_set_kernel_stack((u64)rsp0);
    syscall_state.kernel_rsp = (u64)rsp0;
}
