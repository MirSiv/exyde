#ifndef EXYDE_USERSPACE_CONSOLE_CLIENT_H
#define EXYDE_USERSPACE_CONSOLE_CLIENT_H

#include <stddef.h>
#include <exyde/micro.h>

/* Client side of the console RPC protocol.
 *
 * After console_client_init(ch_req, ch_resp) succeeds, libc routes
 * fd 0/1/2 through the console server instead of the kernel.  Before
 * that call, libc keeps the kernel path (transitional SYS_WRITE /
 * SYS_READ), so programs that never touch the console server still
 * work -- useful for bootstrap itself.
 *
 * Both client and server run in the same process model; only init
 * currently initialises the console. */

int  console_client_init(exyde_handle_t ch_req, exyde_handle_t ch_resp);

/* 1 if console_client_init has been called successfully, else 0.
 * libc's unistd.c checks this to decide between RPC and kernel. */
int  console_client_ready(void);

long console_client_write(const void *buf, size_t len);
long console_client_read (void *buf, size_t len);
int  console_client_shutdown(void);

#endif /* EXYDE_USERSPACE_CONSOLE_CLIENT_H */
