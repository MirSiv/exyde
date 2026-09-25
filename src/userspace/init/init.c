#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>

/* init for Phase 11.2.3. */

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

static int cmp_str(const void *a, const void *b) {
    const char *x = *(const char *const *)a;
    const char *y = *(const char *const *)b;
    return strcmp(x, y);
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    printf("init: hello from userspace (C)\n");

    /* --- abs / labs --- */
    printf("init: abs    [%d] [%d] [%ld] [%ld]\n",
           abs(5), abs(-5), labs(1234567890L), labs(-1234567890L));

    /* --- qsort on ints --- */
    {
        int v[] = { 5, 1, 4, 2, 8, 0, 9, 3, 7, 6, 5, 2 };
        size_t n = sizeof(v) / sizeof(v[0]);
        qsort(v, n, sizeof(v[0]), cmp_int);
        printf("init: qsort int ");
        for (size_t i = 0; i < n; ++i) printf(" %d", v[i]);
        printf("\n");
    }

    /* --- qsort on strings --- */
    {
        const char *s[] = { "banana", "apple", "cherry", "date", "elderberry" };
        size_t n = sizeof(s) / sizeof(s[0]);
        qsort(s, n, sizeof(s[0]), cmp_str);
        printf("init: qsort str");
        for (size_t i = 0; i < n; ++i) printf(" %s", s[i]);
        printf("\n");
    }

    /* --- bsearch on the sorted ints --- */
    {
        int v[] = { 1, 3, 5, 7, 9, 11, 13 };
        size_t n = sizeof(v) / sizeof(v[0]);
        int key = 7;
        int *hit = (int *)bsearch(&key, v, n, sizeof(v[0]), cmp_int);
        int miss_key = 4;
        int *miss = (int *)bsearch(&miss_key, v, n, sizeof(v[0]), cmp_int);
        printf("init: bsearch [%d] [%s]\n",
               hit ? *hit : -1,
               miss ? "found" : "missing");
    }

    /* --- string.h regression --- */
    char buf[64];
    strcpy(buf, "hello, ");
    strcat(buf, "world");
    printf("init: cat    [%s]\n", buf);

    {
        char tokbuf[] = "a,b,,c,";
        char *t;
        printf("init: tok   ");
        for (t = strtok(tokbuf, ","); t; t = strtok(NULL, ",")) {
            printf(" [%s]", t);
        }
        printf(" [end]\n");
    }

    /* --- numeric conversion regression --- */
    printf("init: strtol [%ld] [%ld] [%ld]\n",
           strtol("-17", NULL, 0),
           strtol("0xff", NULL, 0),
           strtol("101010", NULL, 2));

    /* --- strerror / perror --- */
    printf("init: strerror [%s] [%s] [%s] [%s]\n",
           strerror(ENOENT),
           strerror(EINVAL),
           strerror(ERANGE),
           strerror(ENOSYS));
    /* strerror returns a non-reentrant static buffer; copy each
     * result out before calling strerror again. */
    {
        char s1[64], s2[64];
        strcpy(s1, strerror(999));
        strcpy(s2, strerror(-1));
        printf("init: strerror unknown [%s] [%s]\n", s1, s2);
    }

    errno = ENOENT;
    perror("init: perror-test");
    errno = 0;
    perror("init: perror-empty");

    /* --- allocator regression --- */
    char *a = (char *)malloc(64);
    char *b = (char *)malloc(64);
    char *c = (char *)malloc(64);
    if (!a || !b || !c) { printf("init: malloc failed\n"); return 1; }
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

    unsigned char *z = (unsigned char *)calloc(32, 1);
    if (!z) { printf("init: calloc failed\n"); return 4; }
    for (int i = 0; i < 32; ++i) {
        if (z[i] != 0) {
            printf("init: calloc not zero at index %d\n", i);
            return 5;
        }
    }
    free(z);

    char *r = (char *)malloc(32);
    if (!r) { printf("init: realloc setup failed\n"); return 6; }
    strcpy(r, "small");
    r = (char *)realloc(r, 128);
    if (!r || strcmp(r, "small") != 0) {
        printf("init: realloc grow failed\n");
        return 7;
    }
    free(r);
    free(c);
    free(d);

    printf("init: heap ok\n");
    return 0;
}
