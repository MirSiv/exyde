#ifndef EXYDE_IRQ_H
#define EXYDE_IRQ_H

#include <exyde/types.h>

#define IRQ_COUNT 16

typedef void (*irq_handler_t)(u8 irq);

/* --- Frontend: used by core and subsystems --- */

/* Register a handler for an IRQ line (0..IRQ_COUNT-1).
 * Passing NULL unregisters the handler. */
void irq_register(u8 irq, irq_handler_t handler);

/* Dispatch one IRQ: invoke the registered handler (if any),
 * then send end-of-interrupt to the controller.
 * Called from the arch interrupt entry path. */
void irq_dispatch(u8 irq);

/* --- Backend: implemented by arch/<arch>/. --- */

/* Bring up the arch interrupt controller.
 * Typical responsibility: remap the PIC, mask everything, unmask IRQ0. */
void arch_irq_init(void);

/* Send end-of-interrupt for the given IRQ line. */
void arch_irq_eoi(u8 irq);

#endif /* EXYDE_IRQ_H */
