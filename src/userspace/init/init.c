#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <exyde/micro.h>

/* init for Phase 11.3.1 (environment). */

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
    (void)argc; (void)argv;

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

    /* --- string.h tail regression (Phase 11.3.2) --- */
    {
        /* strpbrk: first char of s that is in accept. */
        const char *p1 = strpbrk("hello, world", " ,");
        const char *p2 = strpbrk("hello", "xyz");
        const char *p3 = strpbrk("hello", "");
        printf("init: strpbrk [%s] [%s] [%s]\n",
               p1 ? p1 : "(null)",
               p2 ? p2 : "(null)",
               p3 ? p3 : "(null)");
        if (!p1 || strcmp(p1, ", world") != 0) return 30;
        if (p2) return 31;
        if (p3) return 32;

        /* memchr: bounded search, must not read past n bytes. */
        const char h[] = "hello";
        const char *m1 = (const char *)memchr(h, 'l', 5);    /* -> h+2 */
        const char *m2 = (const char *)memchr(h, 'l', 2);    /* miss  */
        const char *m3 = (const char *)memchr(h, '\0', 5);  /* miss  */
        const char *m4 = (const char *)memchr(h, '\0', 6);  /* -> h+5 */
        const char *m5 = (const char *)memchr(h, 'h', 0);    /* n=0 miss */
        printf("init: memchr [%d] [%s] [%s] [%d] [%s]\n",
               m1 ? (int)(m1 - h) : -1,
               m2 ? "hit" : "miss",
               m3 ? "hit" : "miss",
               m4 ? (int)(m4 - h) : -1,
               m5 ? "hit" : "miss");
        if (!m1 || (m1 - h) != 2) return 33;
        if (m2) return 34;
        if (m3) return 35;
        if (!m4 || (m4 - h) != 5) return 36;
        if (m5) return 37;

        /* strspn / strcspn edge cases. */
        size_t s1 = strspn("aaabbb", "a");    /* 3 */
        size_t s2 = strspn("abc", "");        /* 0 */
        size_t s3 = strcspn("abcdef", "cd");  /* 2 */
        size_t s4 = strcspn("abcdef", "");    /* 6 */
        printf("init: strspn/cspn [%zu] [%zu] [%zu] [%zu]\n",
               s1, s2, s3, s4);
        if (s1 != 3 || s2 != 0 || s3 != 2 || s4 != 6) return 38;

        /* strncpy: NUL-pad, n=0 writes nothing, boundary at n. */
        char nc1[8];
        memset(nc1, 'X', sizeof nc1);
        strncpy(nc1, "abc", 5);
        if (nc1[0] != 'a' || nc1[1] != 'b' || nc1[2] != 'c' ||
            nc1[3] != 0   || nc1[4] != 0   || nc1[5] != 'X') return 39;

        char nc2[4] = "abc";
        strncpy(nc2, "xyz", 0);
        if (strcmp(nc2, "abc") != 0) return 40;

        /* strncat: bounded append, always NUL-terminated, n=0 no-op. */
        char na1[16] = "foo";
        strncat(na1, "barbaz", 3);
        if (strcmp(na1, "foobar") != 0) return 41;

        char na2[16] = "foo";
        strncat(na2, "bar", 0);
        if (strcmp(na2, "foo") != 0) return 42;

        printf("init: strncpy/strncat ok\n");
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

    /* --- snprintf / sprintf regression --- */
    {
        char sb[64];
        int n = snprintf(sb, sizeof(sb), "%d/%s/%04x", 42, "abc", 0xbeef);
        printf("init: snprintf [%s] [%d]\n", sb, n);
    }
    {
        char tb[8];
        int tn = snprintf(tb, sizeof(tb), "abcdefghijklmn");
        printf("init: snprintf trunc [%s] [%d]\n", tb, tn);
    }
    {
        char ob[1];
        int on = snprintf(ob, sizeof(ob), "xyz");
        printf("init: snprintf n=1 [%d] [%d]\n",
               (int)(unsigned char)ob[0], on);
    }
    {
        int zn = snprintf(NULL, 0, "%d-%d-%d", 1, 22, 333);
        printf("init: snprintf n=0 [%d]\n", zn);
    }
    {
        char sp[64];
        int sn = sprintf(sp, "x=%d y=%s", -7, "ok");
        printf("init: sprintf [%s] [%d]\n", sp, sn);
    }

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

    /* --- environment regression (Phase 11.3.1) ---
     *
     * Deliberately the LAST block in main(): the ENOMEM case below
     * exhausts the arena on purpose and never frees the exhaust
     * allocations.  That is fine because the process exits right
     * after, and the heap is torn down with it.  Nothing else in
     * init runs once the arena is exhausted, so printf (which does
     * not allocate) still works.
     */
    {
        /* (8) crt0 must have installed environ == envp before main. */
        if (environ != envp) {
            printf("init: env FAIL environ != envp\n");
            return 10;
        }

        /* (1) getenv of an existing initial-env variable. */
        const char *path0 = getenv("PATH");
        printf("init: env get initial [%s]\n", path0 ? path0 : "(null)");
        if (!path0 || strcmp(path0, "/") != 0) return 11;

        /* (2) getenv of a missing variable. */
        char *miss = getenv("EXYDE_NO_SUCH_VAR");
        printf("init: env get missing [%s]\n", miss ? miss : "(null)");
        if (miss) return 12;

        /* (3) setenv overwrite=0 must not clobber an existing value. */
        int r3 = setenv("PATH", "/overwritten", 0);
        const char *path1 = getenv("PATH");
        printf("init: env setenv nooverwrite [%d] [%s]\n",
               r3, path1 ? path1 : "(null)");
        if (r3 != 0 || !path1 || strcmp(path1, "/") != 0) return 13;

        /* (4) setenv overwrite=1 must replace it.  The original "/"
         * lives on the initial envp stack, so no free() must occur;
         * the new value is heap-owned. */
        int r4 = setenv("PATH", "/new", 1);
        const char *path2 = getenv("PATH");
        printf("init: env setenv overwrite [%d] [%s]\n",
               r4, path2 ? path2 : "(null)");
        if (r4 != 0 || !path2 || strcmp(path2, "/new") != 0) return 14;

        /* (5) setenv of a brand-new variable. */
        int r5 = setenv("EXYDE", "1", 0);
        const char *e1 = getenv("EXYDE");
        printf("init: env setenv new [%d] [%s]\n",
               r5, e1 ? e1 : "(null)");
        if (r5 != 0 || !e1 || strcmp(e1, "1") != 0) return 15;

        /* (6) unsetenv of an existing variable. */
        int r6 = unsetenv("EXYDE");
        const char *e2 = getenv("EXYDE");
        printf("init: env unset existing [%d] [%s]\n",
               r6, e2 ? e2 : "(null)");
        if (r6 != 0 || e2) return 16;

        /* (7) unsetenv of a missing variable is a no-op success. */
        int r7 = unsetenv("EXYDE_NO_SUCH_VAR");
        printf("init: env unset missing [%d]\n", r7);
        if (r7 != 0) return 17;

        /* (8) EINVAL on empty name. */
        errno = 0;
        int r8 = setenv("", "x", 0);
        printf("init: env einval empty [%d] [%d]\n", r8, errno);
        if (r8 != -1 || errno != EINVAL) return 18;

        /* (9) EINVAL on name containing '='. */
        errno = 0;
        int r9 = setenv("A=B", "x", 0);
        printf("init: env einval eq [%d] [%d]\n", r9, errno);
        if (r9 != -1 || errno != EINVAL) return 19;

        /* (10) growth path: many setenv, verify, many unsetenv, then
         * check that environ survived realloc and older entries stay. */
        for (int i = 0; i < 20; ++i) {
            char nm[32], vl[32];
            snprintf(nm, sizeof nm, "EXYDE_TEST_%d", i);
            snprintf(vl, sizeof vl, "v%d", i);
            if (setenv(nm, vl, 1) != 0) {
                printf("init: env growth setenv failed at %d\n", i);
                return 20;
            }
        }
        for (int i = 0; i < 20; ++i) {
            char nm[32], want[32];
            snprintf(nm,   sizeof nm,   "EXYDE_TEST_%d", i);
            snprintf(want, sizeof want, "v%d", i);
            char *got = getenv(nm);
            if (!got || strcmp(got, want) != 0) {
                printf("init: env growth verify failed at %d\n", i);
                return 21;
            }
        }
        const char *path3 = getenv("PATH");
        if (!path3 || strcmp(path3, "/new") != 0) {
            printf("init: env PATH lost after growth\n");
            return 22;
        }
        printf("init: env growth ok (20 vars, PATH preserved)\n");

        for (int i = 0; i < 20; ++i) {
            char nm[32];
            snprintf(nm, sizeof nm, "EXYDE_TEST_%d", i);
            if (unsetenv(nm) != 0) {
                printf("init: env shrink unsetenv failed at %d\n", i);
                return 23;
            }
        }

        /* (11) ENOMEM.
         *
         * The boundary-tag allocator grows the arena by sbrk() in
         * 64 KiB chunks, so a single exhausted malloc(64 KiB) can
         * leave large free fragments behind.  Walk the request size
         * down from 64 KiB to 16 so those fragments get consumed
         * too, then setenv's own allocation must fail.
         *
         * The exhaust allocations are intentionally never freed --
         * see the block comment above. */
        size_t sz = 64 * 1024;
        int n_holes = 0;
        while (sz >= 16) {
            void *p = malloc(sz);
            if (p) { ++n_holes; continue; }
            sz /= 2;
        }

        errno = 0;
        int r11 = setenv("EXYDE_ENOMEM", "x", 0);
        int e11 = errno;
        if (r11 != -1 || e11 != ENOMEM) {
            printf("init: env enomem unexpected [%d] [%d] holes=%d\n",
                   r11, e11, n_holes);
            return 24;
        }
        if (getenv("EXYDE_ENOMEM")) {
            printf("init: env enomem left a stale entry\n");
            return 25;
        }
        printf("init: env enomem ok (holes=%d)\n", n_holes);

        /* (12) getenv still works after everything. */
        const char *path4 = getenv("PATH");
        printf("init: env final PATH [%s]\n", path4 ? path4 : "(null)");
        if (!path4 || strcmp(path4, "/new") != 0) return 26;
    }

    /* --- microkernel ABI regression (Phase 11.5.0) --- */
    {
        /* IPC: create + try_send + try_recv + EAGAIN + close. */
        exyde_handle_t ch = exyde_ipc_create(16, 2);
        if (ch == EXYDE_HANDLE_INVALID) {
            printf("init: micro FAIL ipc_create errno=%d\n", errno);
            return 30;
        }

        char msg1[16] = "msg-1";
        char msg2[16] = "msg-2";
        char msg3[16] = "msg-3";
        if (exyde_ipc_try_send(ch, msg1, 16) != 0) return 31;
        if (exyde_ipc_try_send(ch, msg2, 16) != 0) return 32;

        errno = 0;
        if (exyde_ipc_try_send(ch, msg3, 16) != -1 || errno != EAGAIN)
            return 33;

        char out[16];
        if (exyde_ipc_recv(ch, out, 16) != 16) return 34;
        if (strcmp(out, "msg-1") != 0) return 35;
        if (exyde_ipc_recv(ch, out, 16) != 16) return 36;
        if (strcmp(out, "msg-2") != 0) return 37;

        errno = 0;
        if (exyde_ipc_try_recv(ch, out, 16) != -1 || errno != EAGAIN)
            return 38;

        if (exyde_handle_close(ch) != 0) return 39;

        printf("init: micro ipc ok\n");

        /* IPC: EINVAL on bad create args. */
        errno = 0;
        if (exyde_ipc_create(0, 2) != EXYDE_HANDLE_INVALID || errno != EINVAL)
            return 40;
        errno = 0;
        if (exyde_ipc_create(16, 0) != EXYDE_HANDLE_INVALID || errno != EINVAL)
            return 41;
        errno = 0;
        if (exyde_ipc_create(9999, 2) != EXYDE_HANDLE_INVALID || errno != EINVAL)
            return 42;

        /* MAP / UNMAP. */
        unsigned char *p = (unsigned char *)exyde_map(NULL, 1, EXYDE_MAP_WRITE);
        if (!p) {
            printf("init: micro FAIL map errno=%d\n", errno);
            return 43;
        }
        p[0] = 0xAB;
        p[EXYDE_PAGE_SIZE - 1] = 0xCD;
        if (p[0] != 0xAB || p[EXYDE_PAGE_SIZE - 1] != 0xCD) return 44;
        if (exyde_unmap(p, 1) != 0) return 45;

        /* MAP: read-only (flags = 0) is accepted; write-only is the
         * common case.  Just check it does not fail. */
        void *ro = exyde_map(NULL, 1, 0);
        if (!ro) return 46;
        if (exyde_unmap(ro, 1) != 0) return 47;

        /* MAP: npages = 0 -> EINVAL. */
        errno = 0;
        if (exyde_map(NULL, 0, EXYDE_MAP_WRITE) != NULL || errno != EINVAL)
            return 48;

        /* MAP: unknown flag -> EINVAL (kernel only accepts VM_WRITE). */
        errno = 0;
        if (exyde_map(NULL, 1, 0x8000u) != NULL || errno != EINVAL)
            return 49;

        /* UNMAP: unaligned address -> EINVAL. */
        errno = 0;
        if (exyde_unmap((void *)(uintptr_t)1, 1) != -1 || errno != EINVAL)
            return 50;

        /* YIELD always succeeds. */
        if (exyde_yield() != 0) return 51;

        printf("init: micro map/yield ok\n");
    }

    return 0;
}
