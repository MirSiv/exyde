#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* `environ` is the public pointer to the process environment.
 *
 * Before the first mutation it points directly at the initial envp
 * array built by the kernel on the process stack (crt0.asm stores it).
 * That array and its strings belong to the stack frame -- they are
 * NOT ours to free().
 *
 * On the first mutation we promote: copy the pointer array onto the
 * heap, keep a parallel `owned[]` flag array that records which
 * entries are heap strings we created ourselves (via setenv), and
 * point `environ` at the heap array.  Strings inherited from the
 * initial envp keep owned[i] == 0 forever.
 *
 * Naming:
 *   env_arr      -- heap pointer array (== environ after promote)
 *   env_owned    -- parallel to env_arr; 1 => env_arr[i] is ours to free
 *   env_cap      -- slots available in env_arr (>= env_count + 1)
 *   env_count    -- NAME=VALUE entries (NULL terminator excluded)
 *   env_is_owned -- 0 while environ still points at the stack array
 *
 * getenv() never allocates and never mutates state. */

char **environ = NULL;

static char          **env_arr;
static unsigned char  *env_owned;
static size_t          env_cap;
static size_t          env_count;
static int             env_is_owned;

/* --- helpers --------------------------------------------------------- */

static size_t count_env(char **e) {
    size_t n = 0;
    if (e) while (e[n]) ++n;
    return n;
}

/* Promote the stack-backed environ to a heap-backed one.  Idempotent. */
static int promote(void) {
    if (env_is_owned) return 0;

    size_t n   = count_env(environ);
    size_t cap = n + 8;

    char **arr = (char **)malloc(cap * sizeof(char *));
    if (!arr) { errno = ENOMEM; return -1; }
    unsigned char *own = (unsigned char *)calloc(cap, 1);
    if (!own) { free(arr); errno = ENOMEM; return -1; }

    for (size_t i = 0; i < n; ++i) arr[i] = environ[i];
    arr[n] = NULL;

    env_arr      = arr;
    env_owned    = own;
    env_cap      = cap;
    env_count    = n;
    env_is_owned = 1;
    environ      = arr;
    return 0;
}

/* Ensure room for env_count + 1 entries plus the NULL terminator. */
static int ensure_slot(void) {
    if (env_cap >= env_count + 2) return 0;

    size_t ncap = env_cap ? env_cap * 2 : 16;

    unsigned char *nown = (unsigned char *)malloc(ncap);
    if (!nown) { errno = ENOMEM; return -1; }
    for (size_t i = 0; i < env_cap; ++i) nown[i] = env_owned[i];
    for (size_t i = env_cap; i < ncap; ++i) nown[i] = 0;

    char **narr = (char **)realloc(env_arr, ncap * sizeof(char *));
    if (!narr) { free(nown); errno = ENOMEM; return -1; }

    free(env_owned);
    env_owned = nown;
    env_arr   = narr;
    environ   = narr;
    env_cap   = ncap;
    return 0;
}

/* Allocate a fresh "name=value" string. */
static char *make_entry(const char *name, size_t nl,
                        const char *value, size_t vl) {
    char *s = (char *)malloc(nl + 1 + vl + 1);
    if (!s) return NULL;
    memcpy(s, name, nl);
    s[nl] = '=';
    memcpy(s + nl + 1, value, vl + 1);   /* includes the NUL */
    return s;
}

/* --- API ------------------------------------------------------------- */

char *getenv(const char *name) {
    if (!name || !*name || strchr(name, '=')) return NULL;
    if (!environ) return NULL;
    size_t nl = strlen(name);
    for (char **e = environ; *e; ++e) {
        if (strncmp(*e, name, nl) == 0 && (*e)[nl] == '=') return *e + nl + 1;
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!value)                               { errno = EINVAL; return -1; }

    size_t nl = strlen(name);
    size_t vl = strlen(value);

    /* Existing entry?  Linear scan, same rule as getenv(). */
    char **hit = NULL;
    if (environ) {
        for (char **e = environ; *e; ++e) {
            if (strncmp(*e, name, nl) == 0 && (*e)[nl] == '=') {
                hit = e;
                break;
            }
        }
    }

    if (hit) {
        if (!overwrite) return 0;
        size_t idx = (size_t)(hit - environ);   /* before promote! */
        if (promote() < 0) return -1;

        char *nstr = make_entry(name, nl, value, vl);
        if (!nstr) { errno = ENOMEM; return -1; }

        /* Only free strings we allocated ourselves.  After promote the
         * slot is always owned[i] == 0 for the initial strings, so the
         * guard is exactly what keeps free() off the stack. */
        if (env_owned[idx]) free(env_arr[idx]);
        env_arr[idx]   = nstr;
        env_owned[idx] = 1;
        return 0;
    }

    /* New entry. */
    if (promote() < 0)     return -1;
    if (ensure_slot() < 0) return -1;

    char *nstr = make_entry(name, nl, value, vl);
    if (!nstr) { errno = ENOMEM; return -1; }

    env_arr[env_count]   = nstr;
    env_owned[env_count] = 1;
    env_count++;
    env_arr[env_count]   = NULL;
    return 0;
}

int unsetenv(const char *name) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!environ) return 0;

    size_t nl = strlen(name);
    char **hit = NULL;
    for (char **e = environ; *e; ++e) {
        if (strncmp(*e, name, nl) == 0 && (*e)[nl] == '=') { hit = e; break; }
    }
    if (!hit) return 0;

    size_t idx = (size_t)(hit - environ);   /* before promote! */
    if (promote() < 0) return -1;

    if (env_owned[idx]) free(env_arr[idx]);

    for (size_t i = idx; i + 1 < env_count; ++i) {
        env_arr[i]   = env_arr[i + 1];
        env_owned[i] = env_owned[i + 1];
    }
    env_count--;
    env_arr[env_count]   = NULL;
    env_owned[env_count] = 0;
    return 0;
}
