#include <exyde/channel.h>
#include <exyde/heap.h>
#include <exyde/sched.h>
#include <exyde/arch.h>
#include <exyde/errno.h>
#include <exyde/panic.h>

static void copy_bytes(u8 *dst, const u8 *src, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
}

channel_t *channel_create(size_t capacity, size_t msg_size) {
    if (capacity == 0 || msg_size == 0) return (channel_t *)0;

    channel_t *c = (channel_t *)kzalloc(sizeof(channel_t));
    if (!c) return (channel_t *)0;

    size_t bytes = capacity * msg_size;
    c->buffer = (u8 *)kmalloc(bytes);
    if (!c->buffer) {
        kfree(c);
        return (channel_t *)0;
    }
    for (size_t i = 0; i < bytes; ++i) c->buffer[i] = 0;

    c->caps = (chan_cap_t *)kzalloc(capacity * sizeof(chan_cap_t));
    if (!c->caps) {
        kfree(c->buffer);
        kfree(c);
        return (channel_t *)0;
    }

    c->capacity = capacity;
    c->msg_size = msg_size;
    c->count    = 0;
    c->head     = 0;
    c->tail     = 0;
    waitq_init(&c->senders);
    waitq_init(&c->receivers);
    return c;
}

void channel_destroy(channel_t *c) {
    if (!c) return;
    if (!waitq_empty(&c->senders))   panic("channel: destroy with senders waiting");
    if (!waitq_empty(&c->receivers)) panic("channel: destroy with receivers waiting");
    kfree(c->caps);
    kfree(c->buffer);
    kfree(c);
}

size_t channel_count(const channel_t *c)    { return c->count; }
size_t channel_capacity(const channel_t *c) { return c->capacity; }

/* ---- internal (caller holds IRQs disabled) -------------------------- */

/* Write the head slot's bytes into msg_out.  Does not consume. */
static void peek_copy(const channel_t *c, const u8 *slot, void *msg_out) {
    copy_bytes((u8 *)msg_out, slot, c->msg_size);
}

/* Append one message.  Caller holds IRQs disabled. */
static void enqueue(channel_t *c, const void *msg_in,
                    u32 cap_kind, u32 cap_rights, void *cap_obj,
                    bool has_cap, bool cap_owns) {
    u8 *slot = c->buffer + c->tail * c->msg_size;
    copy_bytes(slot, (const u8 *)msg_in, c->msg_size);

    chan_cap_t *cs = &c->caps[c->tail];
    if (has_cap) {
        cs->kind   = cap_kind;
        cs->rights = cap_rights;
        cs->object = cap_obj;
        cs->in_use = 1;
        cs->owns   = cap_owns ? 1 : 0;
    } else {
        cs->kind   = 0;
        cs->rights = 0;
        cs->object = (void *)0;
        cs->in_use = 0;
        cs->owns   = 0;
    }

    c->tail = (c->tail + 1) % c->capacity;
    c->count++;

    thread_t *w = waitq_pop(&c->receivers);
    if (w) sched_unblock(w);
}

/* ---- peek / commit -------------------------------------------------- */

int channel_peek_try(channel_t *c, u8 **out_slot, chan_cap_t **out_cap) {
    if (c->count == 0) return -EAGAIN;
    *out_slot = c->buffer + c->head * c->msg_size;
    *out_cap  = &c->caps[c->head];
    return 0;
}

int channel_peek_blocking(channel_t *c, u8 **out_slot, chan_cap_t **out_cap) {
    while (c->count == 0) {
        waitq_push(&c->receivers, thread_current());
        sched_block();
    }
    *out_slot = c->buffer + c->head * c->msg_size;
    *out_cap  = &c->caps[c->head];
    return 0;
}

void channel_commit(channel_t *c) {
    c->caps[c->head].kind   = 0;
    c->caps[c->head].rights = 0;
    c->caps[c->head].object = (void *)0;
    c->caps[c->head].in_use = 0;
    c->caps[c->head].owns   = 0;

    c->head = (c->head + 1) % c->capacity;
    c->count--;

    thread_t *w = waitq_pop(&c->senders);
    if (w) sched_unblock(w);
}

/* ---- plain send / recv ---------------------------------------------- */

bool channel_try_send(channel_t *c, const void *msg_in) {
    arch_irqs_disable();
    if (c->count == c->capacity) {
        arch_irqs_enable();
        return false;
    }
    enqueue(c, msg_in, 0, 0, (void *)0, false, false);
    arch_irqs_enable();
    return true;
}

bool channel_send(channel_t *c, const void *msg_in) {
    arch_irqs_disable();
    while (c->count == c->capacity) {
        waitq_push(&c->senders, thread_current());
        sched_block();
    }
    enqueue(c, msg_in, 0, 0, (void *)0, false, false);
    arch_irqs_enable();
    return true;
}

bool channel_try_recv(channel_t *c, void *msg_out) {
    arch_irqs_disable();
    u8 *slot; chan_cap_t *cs;
    if (channel_peek_try(c, &slot, &cs) != 0) {
        arch_irqs_enable();
        return false;
    }
    /* Refuse silently to drop a capability. */
    if (cs->in_use) {
        arch_irqs_enable();
        return false;
    }
    peek_copy(c, slot, msg_out);
    channel_commit(c);
    arch_irqs_enable();
    return true;
}

bool channel_recv(channel_t *c, void *msg_out) {
    arch_irqs_disable();
    u8 *slot; chan_cap_t *cs;
    channel_peek_blocking(c, &slot, &cs);
    /* Refuse silently to drop a capability.  The message stays in
     * the channel; the caller will loop and eventually panic or
     * use channel_recv_cap. */
    if (cs->in_use) {
        arch_irqs_enable();
        return false;
    }
    peek_copy(c, slot, msg_out);
    channel_commit(c);
    arch_irqs_enable();
    return true;
}

/* ---- send / recv with capability ------------------------------------ */

int channel_send_cap(channel_t *c, const void *msg_in,
                     u32 cap_kind, u32 cap_rights, void *cap_obj,
                     bool has_cap, bool cap_owns) {
    arch_irqs_disable();
    while (c->count == c->capacity) {
        waitq_push(&c->senders, thread_current());
        sched_block();
    }
    enqueue(c, msg_in, cap_kind, cap_rights, cap_obj, has_cap, cap_owns);
    arch_irqs_enable();
    return 0;
}

int channel_try_recv_cap(channel_t *c, void *msg_out,
                         u32 *out_kind, u32 *out_rights, void **out_obj,
                         bool *out_has_cap, bool *out_owns) {
    arch_irqs_disable();
    u8 *slot; chan_cap_t *cs;
    if (channel_peek_try(c, &slot, &cs) != 0) {
        arch_irqs_enable();
        return -EAGAIN;
    }
    peek_copy(c, slot, msg_out);
    if (cs->in_use) {
        *out_has_cap = true;
        *out_kind    = cs->kind;
        *out_rights  = cs->rights;
        *out_obj     = cs->object;
        *out_owns    = cs->owns != 0;
    } else {
        *out_has_cap = false;
        *out_owns    = false;
    }
    channel_commit(c);
    arch_irqs_enable();
    return 0;
}

int channel_recv_cap(channel_t *c, void *msg_out,
                     u32 *out_kind, u32 *out_rights, void **out_obj,
                     bool *out_has_cap, bool *out_owns) {
    arch_irqs_disable();
    u8 *slot; chan_cap_t *cs;
    channel_peek_blocking(c, &slot, &cs);
    peek_copy(c, slot, msg_out);
    if (cs->in_use) {
        *out_has_cap = true;
        *out_kind    = cs->kind;
        *out_rights  = cs->rights;
        *out_obj     = cs->object;
        *out_owns    = cs->owns != 0;
    } else {
        *out_has_cap = false;
        *out_owns    = false;
    }
    channel_commit(c);
    arch_irqs_enable();
    return 0;
}
