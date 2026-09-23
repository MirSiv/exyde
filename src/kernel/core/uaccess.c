#include <exyde/uaccess.h>
#include <exyde/errno.h>
#include <exyde/pmm.h>

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
    /* Validated: current CR3 is `space`, so direct VA deref is safe and
     * does NOT depend on the bootstrap identity map. */
    u8       *d = (u8 *)(uintptr_t)udst;
    const u8 *s = (const u8 *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return 0;
}

int copy_from_user(vmm_space_t space, void *dst, vaddr_t usrc, size_t n) {
    int r = user_range_ok(space, usrc, n);
    if (r < 0) return r;
    const u8 *s = (const u8 *)(uintptr_t)usrc;
    u8       *d = (u8 *)dst;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return 0;
}

int strncpy_from_user(vmm_space_t space, char *dst, vaddr_t usrc, size_t max) {
    if (max == 0 || !dst) return -EINVAL;
    for (size_t i = 0; i < max; ++i) {
        vaddr_t va = usrc + i;
        if (va < usrc) return -EFAULT;   /* wrap */
        if (user_range_ok(space, va, 1) < 0) return -EFAULT;
        char c = *(const char *)(uintptr_t)va;
        dst[i] = c;
        if (c == '\0') return 0;
    }
    dst[max - 1] = '\0';
    return -ENAMETOOLONG;
}
