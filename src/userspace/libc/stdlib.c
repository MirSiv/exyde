#include <stdlib.h>
#include <unistd.h>

/* malloc/free/calloc/realloc arrive in 11.0.3 together with the
 * brk syscall.  This file currently provides only the exit paths. */

void exit(int status) {
    _exit(status);
}

void abort(void) {
    _exit(134);
}
