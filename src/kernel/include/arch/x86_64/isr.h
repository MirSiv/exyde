#ifndef EXYDE_ARCH_X86_64_ISR_H
#define EXYDE_ARCH_X86_64_ISR_H

#include <exyde/types.h>

/* Frame pushed by the ISR stubs and consumed by isr_dispatch().
 * Layout must match isr_stubs.asm exactly. */
struct regs {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no;
    u64 err_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

/* Stub entry points.  Indices 0..31 are CPU exceptions,
 * 32..47 are PIC IRQ0..IRQ15.  Filled by isr.c. */
extern void *isr_stub_table[48];

/* Called from the assembly stub.
 *
 * For exceptions (int_no < 32): prints a dump and calls panic(),
 * which never returns.
 *
 * For IRQs (32..47): invokes the registered handler via irq_dispatch()
 * and returns normally. */
void isr_dispatch(struct regs *r);

#endif /* EXYDE_ARCH_X86_64_ISR_H */
