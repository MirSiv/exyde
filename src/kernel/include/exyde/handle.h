#ifndef EXYDE_HANDLE_H
#define EXYDE_HANDLE_H

#include <exyde/types.h>

typedef u32 handle_t;

#define HANDLE_INVALID   0xFFFFFFFFu
#define HANDLE_MAX       64u

#define HANDLE_RIGHT_READ   (1u << 0)
#define HANDLE_RIGHT_WRITE  (1u << 1)
#define HANDLE_RIGHT_EXEC   (1u << 2)
#define HANDLE_RIGHT_ALL    (HANDLE_RIGHT_READ | HANDLE_RIGHT_WRITE | HANDLE_RIGHT_EXEC)

/* Kernel object kinds.  These are KERNEL objects only.  Files,
 * directories, fd tables, and consoles are userspace concepts and are
 * NOT kernel handle kinds -- they are negotiated between userspace
 * services over IPC.
 *
 * Kinds 3+ are reserved for future kernel objects (VSPACE, THREAD,
 * IRQ capability, ...). */
#define HANDLE_KIND_NONE       0u
#define HANDLE_KIND_TEST       1u   /* kernel selftest only */
#define HANDLE_KIND_CHANNEL    2u   /* IPC channel (see exyde/channel.h) */

typedef struct {
    u32   kind;
    u32   rights;
    void *object;
    u32   in_use;
    u32   _pad;
} handle_entry_t;

typedef struct {
    handle_entry_t entries[HANDLE_MAX];
    u32            next_hint;
} handle_table_t;

void handle_table_init(handle_table_t *t);

/* Release every handle in the table, invoking `release(kind, object)`
 * for each in-use entry.  `release` may be NULL, in which case only
 * the table is cleared (use for tables that hold no releasable
 * objects).  After this call the table is empty; it is not freed. */
void handle_table_destroy(handle_table_t *t,
                          void (*release)(u32 kind, void *object));

handle_t handle_create(handle_table_t *t, u32 kind, u32 rights, void *object);

bool handle_lookup(const handle_table_t *t, handle_t h, u32 required,
                   void **out_object, u32 *out_kind);

bool handle_close(handle_table_t *t, handle_t h);

#endif /* EXYDE_HANDLE_H */
