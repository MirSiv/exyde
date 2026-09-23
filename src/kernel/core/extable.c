#include <exyde/extable.h>

/* Provided by linker/x86_64.ld.  Each entry is { fault_rip, fixup_rip }
 * as two u64 values.  Lookup is linear; the table is tiny. */
extern const u64 __extable_start[];
extern const u64 __extable_end[];

bool extable_lookup(u64 fault_rip, u64 *fixup) {
    const u64 *p = __extable_start;
    while (p < __extable_end) {
        if (p[0] == fault_rip) {
            if (fixup) *fixup = p[1];
            return true;
        }
        p += 2;
    }
    return false;
}
