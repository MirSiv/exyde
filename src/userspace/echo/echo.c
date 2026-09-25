/* echo.elf -- first Exyde userspace service.
 *
 * Phase 11.5.2.  Spawned by init.elf with a bootstrap channel
 * (obtained via exyde_get_bootstrap()).  Protocol:
 *
 *   receive a 16-byte message; if it is "quit", exit 0;
 *   otherwise send it back unchanged. */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <exyde/micro.h>

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    exyde_handle_t ch = exyde_get_bootstrap();
    if (ch == EXYDE_HANDLE_INVALID) {
        printf("echo: no bootstrap channel\n");
        return 1;
    }
    printf("echo: ready\n");

    for (;;) {
        char msg[16];
        int n = exyde_ipc_recv(ch, msg, 16);
        if (n != 16) {
            printf("echo: recv failed errno=%d\n", errno);
            return 1;
        }

        if (strcmp(msg, "quit") == 0) {
            printf("echo: quitting\n");
            return 0;
        }

        if (exyde_ipc_send(ch, msg, 16) != 0) {
            printf("echo: send failed errno=%d\n", errno);
            return 1;
        }
    }
}
