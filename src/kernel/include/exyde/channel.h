#ifndef EXYDE_CHANNEL_H
#define EXYDE_CHANNEL_H

#include <exyde/types.h>
#include <exyde/waitq.h>

/* In-kernel message channel: bounded ring of fixed-size messages with
 * blocking send/recv.  Each slot may optionally carry ONE capability
 * (kind + rights + object pointer).  Phase 11.5.1.
 *
 * Capability semantics:
 *   - channel_recv_cap extracts the cap if present, otherwise sets
 *     *out_has_cap = false.
 *   - The bool-returning channel_recv / channel_try_recv REFUSE to
 *     consume a message that carries a capability (return false / do
 *     not touch the ring).  Silent capability destruction is not
 *     permitted; the caller must use the _cap variant. */

typedef struct {
    u32   kind;
    u32   rights;
    void *object;
    u32   in_use;
    u32   owns;         /* 1: receiver becomes the owner of object */
    u32   _pad;
} chan_cap_t;

typedef struct channel {
    u8         *buffer;     /* capacity * msg_size bytes            */
    chan_cap_t *caps;       /* capacity cap slots (parallel to ring) */
    size_t      capacity;   /* number of slots                       */
    size_t      msg_size;   /* bytes per message                     */
    size_t      count;      /* slots currently occupied              */
    size_t      head;       /* next slot to read                     */
    size_t      tail;       /* next slot to write                    */
    waitq_t     senders;    /* blocked on full                       */
    waitq_t     receivers;  /* blocked on empty                      */
} channel_t;

channel_t *channel_create(size_t capacity, size_t msg_size);
void       channel_destroy(channel_t *c);

size_t channel_count(const channel_t *c);
size_t channel_capacity(const channel_t *c);

/* ---- plain send/recv (no capability) -------------------------------- */

/* Return true on success.  channel_recv refuses (false, ring
 * untouched) if the next message carries a capability. */
bool channel_send(channel_t *c, const void *msg_in);
bool channel_recv(channel_t *c, void *msg_out);

/* Non-blocking: false if full (send) or empty/refused (recv). */
bool channel_try_send(channel_t *c, const void *msg_in);
bool channel_try_recv(channel_t *c, void *msg_out);

/* ---- send/recv with capability -------------------------------------- */

/* Send with an optional capability.  has_cap == false is exactly
 * equivalent to channel_send.  Blocks while full.  Returns 0. */
int channel_send_cap(channel_t *c, const void *msg_in,
                     u32 cap_kind, u32 cap_rights, void *cap_obj,
                     bool has_cap, bool cap_owns);

/* Recv, extracting the capability if present.  Blocks while empty.
 * Returns 0; *out_has_cap is set true iff the consumed message
 * carried one, and only then are the other out pointers written. */
int channel_recv_cap(channel_t *c, void *msg_out,
                     u32 *out_kind, u32 *out_rights, void **out_obj,
                     bool *out_has_cap, bool *out_owns);

/* Non-blocking variant.  Returns -EAGAIN if the channel is empty. */
int channel_try_recv_cap(channel_t *c, void *msg_out,
                         u32 *out_kind, u32 *out_rights, void **out_obj,
                         bool *out_has_cap, bool *out_owns);

/* ---- peek / commit (used by syscalls for pre-checks) ---------------- */

/* Peek the head slot without consuming.  Caller must hold IRQs
 * disabled for all three.  *_blocking blocks while empty; on return
 * IRQs are still disabled.  Returns 0 or -EAGAIN. */
int  channel_peek_try(channel_t *c, u8 **out_slot, chan_cap_t **out_cap);
int  channel_peek_blocking(channel_t *c, u8 **out_slot, chan_cap_t **out_cap);

/* Consume the peeked head slot, clear its cap, wake one sender. */
void channel_commit(channel_t *c);

#endif /* EXYDE_CHANNEL_H */
