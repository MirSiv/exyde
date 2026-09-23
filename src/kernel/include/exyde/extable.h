#ifndef EXYDE_EXTABLE_H
#define EXYDE_EXTABLE_H

#include <exyde/types.h>

/* Look up the fixup address for a fault at `fault_rip`.
 * Returns true and stores the fixup RIP in *fixup on success,
 * false if the RIP is not covered by an extable entry. */
bool extable_lookup(u64 fault_rip, u64 *fixup);

#endif /* EXYDE_EXTABLE_H */
