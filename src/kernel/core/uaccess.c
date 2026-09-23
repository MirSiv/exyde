#include <exyde/uaccess.h>
#include <exyde/errno.h>
#include <exyde/pmm.h>

/* Architecture-provided raw copy primitives with extable-based fault
 * recovery.  They copy `n` bytes in the CURRENT address space (the
 * caller must have switched CR3 to the target space) and return 0 or
 * -EFAULT.  A kernel-mode #PF on either the user load or the user
 * store is caught by the extable and converted to -EFAULT. */
extern int uaccess_memcpy_to_user  (void *udst, const void *ksrc, size_t n);
extern int uaccess_memcpy_from_user(void *kdst, const void *usrc, size_t n);

/* Wrap-around-safe check that [va, va+n) sits in the user window. */
static bool range_in_user_half(vaddr_t va, size_t n) {
    if (n == 0) return true;
    vaddr_t end = va + n;
    if (end < va)             return false;   /* wrap */
    if (va < USER_VA_BASE)    return false;
    if (end > USER_VA_TOP)    return false;
    return true;
}

int user_range_ok(vmm_space_t space, vaddr_t va, size_t n) {
    if (n == 0) return 0;
    if (!range_in_user_half(va, n)) return -EFAULT;

    vaddr_t end  = va + n;
    vaddr_t page = va & ~((vaddr_t)PAGE_SIZE - 1);
    while (page < end) {
        paddr_t pa;
        u32 flags;
        if (!vmm_query(space, page, &pa, &flags)) return -EFAULT;
        if (!(flags & VM_USER))                   return -EFAULT;
        page += PAGE_SIZE;
    }
    return 0;
}

int copy_to_user(vmm_space_t space, vaddr_t udst, const void *src, size_t n) {
    int r = user_range_ok(space, udst, n);
    if (r < 0) return r;
    /* Fast-path check passed.  The asm helper catches any residual
     * TOCTOU fault (page unmapped between check and store) and
     * returns -EFAULT without panicking. */
    return uaccess_memcpy_to_user((void *)(uintptr_t)udst, src, n);
}

int copy_from_user(vmm_space_t space, void *dst, vaddr_t usrc, size_t n) {
    int r = user_range_ok(space, usrc, n);
    if (r < 0) return r;
    return uaccess_memcpy_from_user(dst, (const void *)(uintptr_t)usrc, n);
}

int strncpy_from_user(vmm_space_t space, char *dst, vaddr_t usrc, size_t max) {
    if (max == 0 || !dst) return -EINVAL;
    for (size_t i = 0; i < max; ++i) {
        vaddr_t va = usrc + i;
        if (va < usrc) return -EFAULT;   /* wrap */
        if (user_range_ok(space, va, 1) < 0) return -EFAULT;
        char c;
        if (uaccess_memcpy_from_user(&c, (const void *)(uintptr_t)va, 1) < 0)
            return -EFAULT;
        dst[i] = c;
        if (c == '\0') return 0;
    }
    dst[max - 1] = '\0';
    return -ENAMETOOLONG;
}
