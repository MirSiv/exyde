#include <exyde/irq.h>
#include <exyde/sched.h>

/* Generic IRQ handler table.  BSS starts zeroed, so all entries are
 * NULL until irq_register() is called. */
static irq_handler_t handlers[IRQ_COUNT];

void irq_register(u8 irq, irq_handler_t handler) {
    if (irq < IRQ_COUNT) {
        handlers[irq] = handler;
    }
}

void irq_dispatch(u8 irq) {
    if (irq < IRQ_COUNT && handlers[irq] != 0) {
        handlers[irq](irq);
    }
    arch_irq_eoi(irq);

    /* Reschedule if any handler (typically the timer) requested it.
     * We are still inside the interrupt frame here, before iretq;
     * switching now means the new thread resumes with our IRQ frame
     * pushed and returns through it cleanly. */
    sched_preempt_point();
}
