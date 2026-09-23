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

/* Object kinds.  Extended as subsystems appear. */
#define HANDLE_KIND_NONE         0u
#define HANDLE_KIND_TEST         1u
#define HANDLE_KIND_CONSOLE_OUT  2u

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

handle_t handle_create(handle_table_t *t, u32 kind, u32 rights, void *object);

bool handle_lookup(const handle_table_t *t, handle_t h, u32 required,
                   void **out_object, u32 *out_kind);

bool handle_close(handle_table_t *t, handle_t h);

#endif /* EXYDE_HANDLE_H */
