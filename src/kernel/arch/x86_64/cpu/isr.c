#include <exyde/types.h>
#include <exyde/console.h>
#include <exyde/panic.h>
#include <exyde/irq.h>
#include <exyde/user.h>
#include <exyde/extable.h>
#include <exyde/sched.h>
#include <exyde/thread.h>
#include <arch/x86_64/isr.h>

#define DECL_STUB(n) extern void isr_stub_##n(void)

DECL_STUB(0);  DECL_STUB(1);  DECL_STUB(2);  DECL_STUB(3);
DECL_STUB(4);  DECL_STUB(5);  DECL_STUB(6);  DECL_STUB(7);
DECL_STUB(8);  DECL_STUB(9);  DECL_STUB(10); DECL_STUB(11);
DECL_STUB(12); DECL_STUB(13); DECL_STUB(14); DECL_STUB(15);
DECL_STUB(16); DECL_STUB(17); DECL_STUB(18); DECL_STUB(19);
DECL_STUB(20); DECL_STUB(21); DECL_STUB(22); DECL_STUB(23);
DECL_STUB(24); DECL_STUB(25); DECL_STUB(26); DECL_STUB(27);
DECL_STUB(28); DECL_STUB(29); DECL_STUB(30); DECL_STUB(31);
DECL_STUB(32); DECL_STUB(33); DECL_STUB(34); DECL_STUB(35);
DECL_STUB(36); DECL_STUB(37); DECL_STUB(38); DECL_STUB(39);
DECL_STUB(40); DECL_STUB(41); DECL_STUB(42); DECL_STUB(43);
DECL_STUB(44); DECL_STUB(45); DECL_STUB(46); DECL_STUB(47);

void *isr_stub_table[48] = {
    (void *)isr_stub_0,  (void *)isr_stub_1,  (void *)isr_stub_2,  (void *)isr_stub_3,
    (void *)isr_stub_4,  (void *)isr_stub_5,  (void *)isr_stub_6,  (void *)isr_stub_7,
    (void *)isr_stub_8,  (void *)isr_stub_9,  (void *)isr_stub_10, (void *)isr_stub_11,
    (void *)isr_stub_12, (void *)isr_stub_13, (void *)isr_stub_14, (void *)isr_stub_15,
    (void *)isr_stub_16, (void *)isr_stub_17, (void *)isr_stub_18, (void *)isr_stub_19,
    (void *)isr_stub_20, (void *)isr_stub_21, (void *)isr_stub_22, (void *)isr_stub_23,
    (void *)isr_stub_24, (void *)isr_stub_25, (void *)isr_stub_26, (void *)isr_stub_27,
    (void *)isr_stub_28, (void *)isr_stub_29, (void *)isr_stub_30, (void *)isr_stub_31,
    (void *)isr_stub_32, (void *)isr_stub_33, (void *)isr_stub_34, (void *)isr_stub_35,
    (void *)isr_stub_36, (void *)isr_stub_37, (void *)isr_stub_38, (void *)isr_stub_39,
    (void *)isr_stub_40, (void *)isr_stub_41, (void *)isr_stub_42, (void *)isr_stub_43,
    (void *)isr_stub_44, (void *)isr_stub_45, (void *)isr_stub_46, (void *)isr_stub_47,
};

static const char *const exception_names[32] = {
    "Divide Error (#DE)", "Debug (#DB)", "NMI Interrupt (#NMI)",
    "Breakpoint (#BP)", "Overflow (#OF)", "BOUND Range Exceeded (#BR)",
    "Invalid Opcode (#UD)", "Device Not Available (#NM)",
    "Double Fault (#DF)", "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)", "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)", "General Protection Fault (#GP)",
    "Page Fault (#PF)", "Reserved", "x87 FPU Error (#MF)",
    "Alignment Check (#AC)", "Machine Check (#MC)",
    "SIMD FP Exception (#XM)", "Virtualization Exception (#VE)",
    "Control Protection (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "VMM Communication (#VC)", "Security Exception (#SX)",
    "Reserved",
};

static arch_user_fault_fn user_fault_handler;

void arch_set_user_fault_handler(arch_user_fault_fn fn) {
    user_fault_handler = fn;
}

/* Dump `count` u64 words starting at `base`.  Used by the panic path.
 * The addresses we dump from (the regs frame and r->rsp) always lie on
 * the current kernel stack, so they are always mapped. */
static void dump_stack_words(u64 base, u32 count) {
    const u64 *p = (const u64 *)(uintptr_t)base;
    for (u32 i = 0; i < count; ++i) {
        console_write("    ");
        console_write_hex(base + (u64)i * 8);
        console_write(": ");
        console_write_hex(p[i]);
        console_write("\n");
    }
}

static void print_thread_id(void) {
    thread_t *t = thread_current();
    if (t && t->name) console_write(t->name);
    else              console_write("(no thread)");
}

static void dump_regs(struct regs *r) {
    console_write("  thread  = ");
    print_thread_id();
    console_write("\n  int_no  = "); console_write_hex(r->int_no);
    console_write("\n  err     = "); console_write_hex(r->err_code);
    console_write("\n  rip     = "); console_write_hex(r->rip);
    console_write("\n  cs      = "); console_write_hex(r->cs);
    console_write("\n  rflags  = "); console_write_hex(r->rflags);
    console_write("\n  rsp     = "); console_write_hex(r->rsp);
    console_write("\n  ss      = "); console_write_hex(r->ss);
    u64 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    console_write("\n  cr2     = "); console_write_hex(cr2);
    console_write("\n");
}

static void dump_and_panic(struct regs *r) {
    console_write("\n[EXCEPTION] ");
    if (r->int_no < 32) console_write(exception_names[r->int_no]);
    else                console_write("Unknown");
    console_write("\n");
    dump_regs(r);
    console_write("  -- stack dump from regs frame --\n");
    dump_stack_words((u64)(uintptr_t)r, 24);
    console_write("\n");
    panic("unhandled CPU exception");
}

/* Try to recover a kernel-mode #PF (vector 14) via the extable.
 * Returns true if RIP was rewritten to a fixup and execution should
 * resume via iretq.  The fixup label is expected to set RAX = -EFAULT
 * and return to the original C caller. */
static bool try_kernel_extable(struct regs *r) {
    u64 fixup = 0;
    if (!extable_lookup(r->rip, &fixup)) return false;
    r->rip = fixup;
    return true;
}

void isr_dispatch(struct regs *r) {
    u64 n = r->int_no;

    if (n < 32) {
        bool from_user = (r->cs & 3u) == 3u;

        if (!from_user && n == 14) {
            if (try_kernel_extable(r)) return;
        }

        if (from_user) {
            if (user_fault_handler) {
                user_fault_t f;
                f.int_no   = r->int_no;
                f.err_code = r->err_code;
                f.rip      = r->rip;
                f.rax      = r->rax;
                f.rsp      = r->rsp;
                user_fault_handler(&f);
                /* handler must not return */
            }
            console_write("\n[USER EXCEPTION] ");
            console_write(exception_names[r->int_no]);
            console_write("\n  rip     = "); console_write_hex(r->rip);
            console_write("\n  err     = "); console_write_hex(r->err_code);
            console_write("\n");
            panic("unhandled user-mode exception");
        }
        dump_and_panic(r);
    }

    if (n < 48) {
        irq_dispatch((u8)(n - 32));

        /* Diagnostic guard for the iretq frame.
         *
         * By the time irq_dispatch() returns, the preemption path may
         * have switched away to another thread and then back.  `r`
         * still points at this thread's own regs frame, which has not
         * moved.  If the frame is going to return to user mode, its
         * CS/SS must be 0x23 / 0x1B; any other value means iretq will
         * #GP with err = SS & ~7.  Do NOT try to paper over it -- we
         * want to see the diagnostic dump and stop. */
        if ((r->cs & 3u) == 3u) {
            if (r->cs != 0x23u || r->ss != 0x1Bu) {
                console_write("\n[IRQ iretq-frame check FAILED]\n");
                console_write("  expected cs=0x23 ss=0x1b\n");
                dump_regs(r);
                console_write("  -- stack dump from regs frame --\n");
                dump_stack_words((u64)(uintptr_t)r, 32);
                console_write("\n");
                panic("bad user-return frame before iretq");
            }
        }
        return;
    }

    dump_and_panic(r);
}
