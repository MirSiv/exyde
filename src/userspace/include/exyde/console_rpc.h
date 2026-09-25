#ifndef EXYDE_USERSPACE_CONSOLE_RPC_H
#define EXYDE_USERSPACE_CONSOLE_RPC_H

#include <stdint.h>

/* Console RPC protocol, Phase 11.5.5.
 *
 * Two channels, same pattern as VFS: requests on ch_req (the
 * server's bootstrap), replies on ch_resp (handed over via
 * CONSOLE_OP_ATTACH with a capability).  Fixed-size messages. */

#define CONSOLE_RPC_MSG_SIZE 1024
#define CONSOLE_RPC_DATA_MAX 512

#define CONSOLE_OP_ATTACH   0
#define CONSOLE_OP_WRITE    1
#define CONSOLE_OP_READ     2
#define CONSOLE_OP_SHUTDOWN 3

struct console_req {
    uint32_t op;
    uint32_t len;                    /* WRITE: bytes in data[]  */
    uint8_t  data[CONSOLE_RPC_DATA_MAX];
};

struct console_rsp {
    int64_t  ret;
    uint32_t len;                    /* READ: bytes in data[]   */
    uint32_t _pad;
    uint8_t  data[CONSOLE_RPC_DATA_MAX];
};

_Static_assert(sizeof(struct console_req) <= CONSOLE_RPC_MSG_SIZE,
               "console_req must fit in one RPC message");
_Static_assert(sizeof(struct console_rsp) <= CONSOLE_RPC_MSG_SIZE,
               "console_rsp must fit in one RPC message");

#endif /* EXYDE_USERSPACE_CONSOLE_RPC_H */
