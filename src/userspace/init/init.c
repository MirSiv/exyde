#include <unistd.h>
#include <string.h>

static const char hello[] = "init: hello from userspace (C)\n";

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    write(STDOUT_FILENO, hello, strlen(hello));
    return 0;
}
