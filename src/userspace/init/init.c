#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static const char hello[]     = "init: hello from userspace (C)\n";
static const char heap_pre[]  = "init: heap buffer = ";
static const char heap_want[] = "hello, malloc!";

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    write(STDOUT_FILENO, hello, strlen(hello));

    char *buf = (char *)malloc(64);
    if (!buf) return 1;

    strcpy(buf, heap_want);

    write(STDOUT_FILENO, heap_pre, strlen(heap_pre));
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\n", 1);

    if (strcmp(buf, heap_want) != 0) return 2;

    free(buf);
    return 0;
}
