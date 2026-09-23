#include <exyde/channel.h>
#include <exyde/heap.h>
#include <exyde/sched.h>
#include <exyde/arch.h>
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
    kfree(c->buffer);
    kfree(c);
}

size_t channel_count(const channel_t *c)    { return c->count; }
size_t channel_capacity(const channel_t *c) { return c->capacity; }

/* All public send/recv take care of IRQ state internally. */

bool channel_try_send(channel_t *c, const void *msg_in) {
    arch_irqs_disable();
    if (c->count == c->capacity) {
        arch_irqs_enable();
        return false;
    }
    u8 *slot = c->buffer + c->tail * c->msg_size;
    copy_bytes(slot, (const u8 *)msg_in, c->msg_size);
    c->tail = (c->tail + 1) % c->capacity;
    c->count++;

    thread_t *w = waitq_pop(&c->receivers);
    if (w) sched_unblock(w);

    arch_irqs_enable();
    return true;
}

bool channel_try_recv(channel_t *c, void *msg_out) {
    arch_irqs_disable();
    if (c->count == 0) {
        arch_irqs_enable();
        return false;
    }
    u8 *slot = c->buffer + c->head * c->msg_size;
    copy_bytes((u8 *)msg_out, slot, c->msg_size);
    c->head = (c->head + 1) % c->capacity;
    c->count--;

    thread_t *w = waitq_pop(&c->senders);
    if (w) sched_unblock(w);

    arch_irqs_enable();
    return true;
}

bool channel_send(channel_t *c, const void *msg_in) {
    arch_irqs_disable();
    while (c->count == c->capacity) {
        waitq_push(&c->senders, thread_current());
        sched_block();
    }
    u8 *slot = c->buffer + c->tail * c->msg_size;
    copy_bytes(slot, (const u8 *)msg_in, c->msg_size);
    c->tail = (c->tail + 1) % c->capacity;
    c->count++;

    thread_t *w = waitq_pop(&c->receivers);
    if (w) sched_unblock(w);

    arch_irqs_enable();
    return true;
}

bool channel_recv(channel_t *c, void *msg_out) {
    arch_irqs_disable();
    while (c->count == 0) {
        waitq_push(&c->receivers, thread_current());
        sched_block();
    }
    u8 *slot = c->buffer + c->head * c->msg_size;
    copy_bytes((u8 *)msg_out, slot, c->msg_size);
    c->head = (c->head + 1) % c->capacity;
    c->count--;

    thread_t *w = waitq_pop(&c->senders);
    if (w) sched_unblock(w);

    arch_irqs_enable();
    return true;
}
