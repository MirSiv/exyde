#ifndef EXYDE_USERSPACE_LIMITS_H
#define EXYDE_USERSPACE_LIMITS_H

/* Minimal <limits.h> for x86_64 LP64.  Only the constants that the
 * userspace libc and init currently need; extend as required. */

#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX

#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535u

#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295u

#define LONG_MIN    (-9223372036854775807L - 1L)
#define LONG_MAX    9223372036854775807L
#define ULONG_MAX   18446744073709551615UL

#endif /* EXYDE_USERSPACE_LIMITS_H */
