#include <exyde/handle.h>

void handle_table_init(handle_table_t *t) {
    for (u32 i = 0; i < HANDLE_MAX; ++i) {
        t->entries[i].kind    = 0;
        t->entries[i].rights  = 0;
        t->entries[i].object  = (void *)0;
        t->entries[i].in_use  = 0;
        t->entries[i]._pad    = 0;
    }
    t->next_hint = 0;
}

handle_t handle_create(handle_table_t *t, u32 kind, u32 rights, void *object) {
    for (u32 i = 0; i < HANDLE_MAX; ++i) {
        u32 idx = (t->next_hint + i) % HANDLE_MAX;
        if (!t->entries[idx].in_use) {
            t->entries[idx].kind   = kind;
            t->entries[idx].rights = rights;
            t->entries[idx].object = object;
            t->entries[idx].in_use = 1;
            t->next_hint = (idx + 1) % HANDLE_MAX;
            return (handle_t)idx;
        }
    }
    return HANDLE_INVALID;
}

bool handle_lookup(const handle_table_t *t, handle_t h, u32 required,
                   void **out_object, u32 *out_kind) {
    if (h >= HANDLE_MAX) return false;
    const handle_entry_t *e = &t->entries[h];
    if (!e->in_use) return false;
    if ((e->rights & required) != required) return false;
    if (out_object) *out_object = e->object;
    if (out_kind)   *out_kind   = e->kind;
    return true;
}

bool handle_close(handle_table_t *t, handle_t h) {
    if (h >= HANDLE_MAX) return false;
    if (!t->entries[h].in_use) return false;
    t->entries[h].in_use = 0;
    t->entries[h].object = (void *)0;
    t->entries[h].rights = 0;
    t->entries[h].kind   = 0;
    return true;
}
