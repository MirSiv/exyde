#ifndef EXYDE_USERSPACE_MICRO_H
#define EXYDE_USERSPACE_MICRO_H

#include <stddef.h>

/* Exyde-native (microkernel) userspace API.  This is not POSIX; it is
 * the direct surface over the kernel ABI for capabilities, IPC, and
 * memory primitives.  POSIX calls (open/read/write/...) are built on
 * top of this in later sub-steps of Phase 11.5. */

typedef unsigned int exyde_handle_t;

#define EXYDE_HANDLE_INVALID 0xFFFFFFFFu

/* Handle rights.  Must match kernel HANDLE_RIGHT_* values. */
#define EXYDE_RIGHT_READ   (1u << 0)
#define EXYDE_RIGHT_WRITE  (1u << 1)
#define EXYDE_RIGHT_EXEC   (1u << 2)

/* SYS_MAP flag.  Must match kernel VM_WRITE.  VM_PRESENT and VM_USER
 * are added by the kernel and are not visible to userspace. */
#define EXYDE_MAP_WRITE    (1u << 1)

#define EXYDE_PAGE_SIZE    4096u

/* IPC ------------------------------------------------------------- */

/* Create a channel.  Returns a handle, or EXYDE_HANDLE_INVALID with
 * errno set on failure. */
exyde_handle_t exyde_ipc_create(unsigned int msg_size,
                                unsigned int capacity);

/* Blocking send / recv.  len must equal the channel's msg_size.
 * recv's max_len must be >= the channel's msg_size.  Return 0 (send)
 * or the actual message size (recv) on success, -1 with errno set on
 * failure. */
int exyde_ipc_send(exyde_handle_t h, const void *buf, unsigned int len);
int exyde_ipc_recv(exyde_handle_t h, void *buf, unsigned int max_len);

/* Non-blocking variants.  Return -1 with errno = EAGAIN when the
 * channel is full (send) or empty (recv). */
int exyde_ipc_try_send(exyde_handle_t h, const void *buf, unsigned int len);
int exyde_ipc_try_recv(exyde_handle_t h, void *buf, unsigned int max_len);

int exyde_handle_close(exyde_handle_t h);

/* Memory primitives ----------------------------------------------- */

/* Map npages anonymous pages.  hint == NULL lets the kernel choose an
 * address; a non-NULL hint must be page-aligned.  Returns the actual
 * base address, or NULL with errno set on failure. */
void *exyde_map(void *hint, unsigned int npages, unsigned int flags);

/* Unmap a range previously obtained from exyde_map.  Return 0 on
 * success, -1 with errno set on failure. */
int exyde_unmap(void *vaddr, unsigned int npages);

/* Yield the CPU.  Return 0. */
int exyde_yield(void);

#endif /* EXYDE_USERSPACE_MICRO_H */
