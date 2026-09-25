#ifndef EXYDE_HANDLE_H
#define EXYDE_HANDLE_H

#include <exyde/types.h>

typedef u32 handle_t;

#define HANDLE_INVALID   0xFFFFFFFFu
#define HANDLE_MAX       64u

#define HANDLE_RIGHT_READ       (1u << 0)
#define HANDLE_RIGHT_WRITE      (1u << 1)
#define HANDLE_RIGHT_EXEC       (1u << 2)
#define HANDLE_RIGHT_TRANSFER   (1u << 3)
#define HANDLE_RIGHT_ALL        (HANDLE_RIGHT_READ  | HANDLE_RIGHT_WRITE | \
                                 HANDLE_RIGHT_EXEC  | HANDLE_RIGHT_TRANSFER)

/* Kernel object kinds.  Kernel objects only. */
#define HANDLE_KIND_NONE       0u
#define HANDLE_KIND_TEST       1u   /* kernel selftest only */
#define HANDLE_KIND_CHANNEL    2u   /* IPC channel (see exyde/channel.h) */
#define HANDLE_KIND_PROCESS    3u   /* process_t (see exyde/process.h) */

/* One capability table entry.
 *
 * owns_object: set to 1 on handle_create.  Cleared by handle_disown
 * (used on IPC_CAP_TRANSFER, so the sender's table_destroy no longer
 * releases the object the receiver now holds).  handle_table_destroy
 * only calls release() for entries where owns_object == 1. */
typedef struct {
    u32   kind;
    u32   rights;
    void *object;
    u32   in_use;
    u32   owns_object;
} handle_entry_t;

/* Release callback.  Called with the kind and object of an entry
 * whose owns_object == 1 (a) when the handle is closed via
 * handle_close, or (b) when the table is destroyed.  May be NULL, in
 * which case no release is performed. */
typedef void (*handle_release_fn)(u32 kind, void *object);

typedef struct {
    handle_entry_t   entries[HANDLE_MAX];
    u32              next_hint;
    handle_release_fn release;
} handle_table_t;

/* t->release is stored and used by both handle_close and
 * handle_table_destroy. */
void handle_table_init(handle_table_t *t, handle_release_fn release);

/* Release every remaining in-use handle.  Calls t->release for each
 * entry with owns_object == 1.  After this the table is empty. */
void handle_table_destroy(handle_table_t *t);

/* True iff at least one slot is free (for IPC pre-checks). */
bool handle_table_has_free_slot(const handle_table_t *t);

/* Create a handle.  handle_create sets owns_object = 1 (the common
 * case).  handle_create_ex lets the caller choose: owns = 0 is used
 * for handles created as a DUPLICATE copy, where the original holder
 * remains responsible for the object's lifetime. */
handle_t handle_create(handle_table_t *t, u32 kind, u32 rights, void *object);
handle_t handle_create_ex(handle_table_t *t, u32 kind, u32 rights,
                          void *object, bool owns);

/* Basic lookup (out pointers may be NULL). */
bool handle_lookup(const handle_table_t *t, handle_t h, u32 required,
                   void **out_object, u32 *out_kind);

/* Full lookup: also returns the entry's rights. */
bool handle_lookup_full(const handle_table_t *t, handle_t h, u32 required,
                        void **out_object, u32 *out_kind, u32 *out_rights);

bool handle_close(handle_table_t *t, handle_t h);

/* Clear owns_object on an open handle without closing it. */
bool handle_disown(handle_table_t *t, handle_t h);

#endif /* EXYDE_HANDLE_H */
