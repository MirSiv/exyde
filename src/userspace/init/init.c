#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* init for Phase 11.1.3.
 *
 * Smoke-tests the allocator:
 *   1. malloc + free + coalescing + reuse (from 11.1.2);
 *   2. calloc zero-fill;
 *   3. realloc: grow in place, grow through the fallback path, shrink.
 */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    printf("init: hello from userspace (C)\n");

    /* 1. basic malloc / free / coalesce */
    char *a = (char *)malloc(64);
    char *b = (char *)malloc(64);
    char *c = (char *)malloc(64);
    if (!a || !b || !c) {
        printf("init: malloc failed\n");
        return 1;
    }
    strcpy(a, "hello, malloc!");
    strcpy(b, "block-b");
    strcpy(c, "block-c");
    printf("init: a=%s b=%s c=%s\n", a, b, c);

    free(b);
    free(a);

    char *d = (char *)malloc(96);
    if (!d) { printf("init: re-malloc failed\n"); return 2; }
    strcpy(d, "reused");
    printf("init: d=%s c=%s\n", d, c);
    if (strcmp(c, "block-c") != 0) {
        printf("init: coalesce test failed, c=%s\n", c);
        return 3;
    }

    /* 2. calloc zero-fill */
    unsigned char *z = (unsigned char *)calloc(32, 1);
    if (!z) { printf("init: calloc failed\n"); return 4; }
    for (int i = 0; i < 32; ++i) {
        if (z[i] != 0) {
            printf("init: calloc not zero at index %d\n", i);
            return 5;
        }
    }
    free(z);

    /* 3. realloc */
    char *r = (char *)malloc(32);
    if (!r) { printf("init: realloc setup failed\n"); return 6; }
    strcpy(r, "small");

    /* 3a. grow in place: next block is free (nothing after r). */
    r = (char *)realloc(r, 128);
    if (!r) { printf("init: realloc grow failed\n"); return 7; }
    if (strcmp(r, "small") != 0) {
        printf("init: realloc grow lost data: %s\n", r);
        return 8;
    }

    /* 3b. shrink: should return the same pointer. */
    char *r_old = r;
    r = (char *)realloc(r, 16);
    if (!r) { printf("init: realloc shrink failed\n"); return 9; }
    if (strcmp(r, "small") != 0) {
        printf("init: realloc shrink lost data: %s\n", r);
        return 10;
    }
    if (r != r_old) {
        printf("init: realloc shrink moved pointer unexpectedly\n");
        return 11;
    }

    /* 3c. grow through the fallback path: force a block in the way. */
    char *blocker = (char *)malloc(64);
    if (!blocker) { printf("init: blocker malloc failed\n"); return 12; }
    strcpy(blocker, "in the way");
    r = (char *)realloc(r, 512);
    if (!r) { printf("init: realloc fallback failed\n"); return 13; }
    if (strcmp(r, "small") != 0) {
        printf("init: realloc fallback lost data: %s\n", r);
        return 14;
    }
    free(blocker);

    /* 3d. realloc(p, 0) frees and returns NULL. */
    void *gone = realloc(r, 0);
    if (gone != NULL) {
        printf("init: realloc(p, 0) did not return NULL\n");
        return 15;
    }
    free(c);
    free(d);

    printf("init: heap ok\n");
    return 0;
}
