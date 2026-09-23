#ifndef EXYDE_UACCESS_H
#define EXYDE_UACCESS_H

#include <exyde/types.h>
#include <exyde/vmm.h>

/* Canonical upper bound of user VA on x86_64.  The kernel lives above
 * it (PML4[0] actually, but PML4[1..255] are user-owned by design). */
#define USER_VA_TOP  0x0000800000000000ULL

/* Validate that [va, va+n) lies entirely in the user half and that
 * every page in the range is mapped with VM_USER in `space`.
 * Returns 0 on success, -EFAULT otherwise.  No memory is touched. */
int user_range_ok(vmm_space_t space, vaddr_t va, size_t n);

/* Copy n bytes kernel->user.  Returns 0 on success, -EFAULT. */
int copy_to_user(vmm_space_t space, vaddr_t udst, const void *src, size_t n);

/* Copy n bytes user->kernel.  Returns 0 on success, -EFAULT. */
int copy_from_user(vmm_space_t space, void *dst, vaddr_t usrc, size_t n);

/* Bounded NUL-terminated string copy from user space.  Writes at most
 * `max` bytes including the terminator.  Returns 0 on success,
 * -EFAULT on invalid user range, -ENAMETOOLONG if it doesn't fit. */
int strncpy_from_user(vmm_space_t space, char *dst, vaddr_t usrc, size_t max);

#endif /* EXYDE_UACCESS_H */
