#include <exyde/handle.h>

void handle_table_init(handle_table_t *t, handle_release_fn release) {
    for (u32 i = 0; i < HANDLE_MAX; ++i) {
        t->entries[i].kind        = HANDLE_KIND_NONE;
        t->entries[i].rights      = 0;
        t->entries[i].object      = (void *)0;
        t->entries[i].in_use      = 0;
        t->entries[i].owns_object = 0;
    }
    t->next_hint = 0;
    t->release   = release;
}

handle_t handle_create_ex(handle_table_t *t, u32 kind, u32 rights,
                          void *object, bool owns) {
    for (u32 i = 0; i < HANDLE_MAX; ++i) {
        u32 idx = (t->next_hint + i) % HANDLE_MAX;
        if (!t->entries[idx].in_use) {
            t->entries[idx].kind        = kind;
            t->entries[idx].rights      = rights;
            t->entries[idx].object      = object;
            t->entries[idx].in_use      = 1;
            t->entries[idx].owns_object = owns ? 1 : 0;
            t->next_hint = (idx + 1) % HANDLE_MAX;
            return (handle_t)idx;
        }
    }
    return HANDLE_INVALID;
}

handle_t handle_create(handle_table_t *t, u32 kind, u32 rights, void *object) {
    return handle_create_ex(t, kind, rights, object, true);
}

bool handle_table_has_free_slot(const handle_table_t *t) {
    for (u32 i = 0; i < HANDLE_MAX; ++i) {
        if (!t->entries[i].in_use) return true;
    }
    return false;
}

bool handle_lookup(const handle_table_t *t, handle_t h, u32 required,
                   void **out_object, u32 *out_kind) {
    return handle_lookup_full(t, h, required, out_object, out_kind, (u32 *)0);
}

bool handle_lookup_full(const handle_table_t *t, handle_t h, u32 required,
                        void **out_object, u32 *out_kind, u32 *out_rights) {
    if (!t || h >= HANDLE_MAX) return false;
    const handle_entry_t *e = &t->entries[h];
    if (!e->in_use) return false;
    if ((e->rights & required) != required) return false;
    if (out_object) *out_object = e->object;
    if (out_kind)   *out_kind   = e->kind;
    if (out_rights) *out_rights = e->rights;
    return true;
}

bool handle_close(handle_table_t *t, handle_t h) {
    if (!t || h >= HANDLE_MAX) return false;
    handle_entry_t *e = &t->entries[h];
    if (!e->in_use) return false;

    /* If this handle owned its object, release it now.  Without this
     * a closed handle's kernel object (a channel, a process) would
     * survive until handle_table_destroy -- for a process that means
     * its whole address space is pinned until the parent exits. */
    if (e->owns_object && t->release) {
        t->release(e->kind, e->object);
    }

    e->kind        = HANDLE_KIND_NONE;
    e->rights      = 0;
    e->object      = (void *)0;
    e->in_use      = 0;
    e->owns_object = 0;
    return true;
}

bool handle_disown(handle_table_t *t, handle_t h) {
    if (!t || h >= HANDLE_MAX) return false;
    if (!t->entries[h].in_use) return false;
    t->entries[h].owns_object = 0;
    return true;
}

void handle_table_destroy(handle_table_t *t) {
    if (!t) return;
    for (u32 i = 0; i < HANDLE_MAX; ++i) {
        handle_entry_t *e = &t->entries[i];
        if (!e->in_use) continue;
        if (e->owns_object && t->release) t->release(e->kind, e->object);
        e->kind        = HANDLE_KIND_NONE;
        e->rights      = 0;
        e->object      = (void *)0;
        e->in_use      = 0;
        e->owns_object = 0;
    }
    t->next_hint = 0;
}
