#ifndef EXYDE_ARCH_H
#define EXYDE_ARCH_H

#include <exyde/types.h>

void arch_init(void);

void arch_irqs_enable(void);
void arch_irqs_disable(void);

/* Save the current IRQ-enable state and disable IRQs.  Returns an
 * opaque token to be passed to arch_irqs_restore().  Nesting-safe:
 * restoring an "IRQs disabled" token leaves them disabled. */
u64  arch_irqs_save_and_disable(void);
void arch_irqs_restore(u64 flags);

#endif /* EXYDE_ARCH_H */
