#ifndef EXYDE_TIMER_H
#define EXYDE_TIMER_H

#include <exyde/types.h>

/* Initialize the system timer to fire at the given frequency.
 * The implementation is arch-specific (PIT on x86_64 for now). */
void timer_init(u32 hz);

#endif /* EXYDE_TIMER_H */
