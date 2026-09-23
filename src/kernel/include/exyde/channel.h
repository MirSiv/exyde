#ifndef EXYDE_CHANNEL_H
#define EXYDE_CHANNEL_H

#include <exyde/types.h>
#include <exyde/waitq.h>

/* In-kernel message channel: bounded ring of fixed-size messages,
 * with blocking send/recv.  Used as the substrate for future
 * userspace channels and pipes. */
typedef struct channel {
    u8       *buffer;       /* capacity * msg_size bytes            */
    size_t    capacity;     /* number of slots                      */
    size_t    msg_size;     /* bytes per message                    */
    size_t    count;        /* slots currently occupied             */
    size_t    head;         /* next slot to read                    */
    size_t    tail;         /* next slot to write                   */
    waitq_t   senders;      /* blocked on full                      */
    waitq_t   receivers;    /* blocked on empty                     */
} channel_t;

/* Allocate a channel with `capacity` slots of `msg_size` bytes each.
 * Returns NULL on failure. */
channel_t *channel_create(size_t capacity, size_t msg_size);

/* Free a channel.  Blocks are not allowed: sender/receiver wait
 * queues must be empty. */
void channel_destroy(channel_t *c);

/* Blocking send / recv.  Return true on success.  msg_in is copied
 * into the ring; msg_out is copied out of it. */
bool channel_send(channel_t *c, const void *msg_in);
bool channel_recv(channel_t *c, void *msg_out);

/* Non-blocking variants.  Return false if the channel is full (send)
 * or empty (recv). */
bool channel_try_send(channel_t *c, const void *msg_in);
bool channel_try_recv(channel_t *c, void *msg_out);

/* Diagnostics. */
size_t channel_count(const channel_t *c);
size_t channel_capacity(const channel_t *c);

#endif /* EXYDE_CHANNEL_H */
