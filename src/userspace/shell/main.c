/* exshell -- Exyde shell, Phase 12.2.
 *
 * Non-interactive build: stdin still does not work reliably from a
 * WSL tty via QEMU -serial stdio (see EXYDE_PHASES.md 12.1), so
 * instead of read(0) the shell walks a fixed script through the
 * same parse/dispatch path that an interactive shell will use.
 * Phase 13 lands a keyboard driver server; switching to interactive
 * is then a three-line change (replace the script loop with
 * read_line()).
 *
 * Architecture (kept deliberately close to a real shell):
 *
 *     line  ->  tokenize()  ->  argv[]  ->  dispatch()  ->  builtin
 *
 * Builtins live in a small table.  Each takes (argc, argv) and
 * returns 0 on success or a non-zero exit code.  The dispatch
 * function returns a special code to mean "quit the shell".
 *
 * Filesystem / process / environment builtins land in 12.3+ on
 * top of libvfs and the microkernel ABI; this substep establishes
 * the framework only. */

#include <unistd.h>
#include <string.h>

#define LINE_MAX  256
#define ARGS_MAX  16

/* ---- I/O helpers ------------------------------------------------- */

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void nl(void) {
    write(1, "\n", 1);
}

/* ---- builtins ---------------------------------------------------- */

typedef int (*builtin_fn)(int argc, char **argv);

/* Printed by `help`. */
struct builtin {
    const char *name;
    const char *help;
    builtin_fn  fn;
};

static int builtin_echo(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (i > 1) out(" ");
        out(argv[i]);
    }
    nl();
    return 0;
}

static int builtin_version(int argc, char **argv) {
    (void)argc; (void)argv;
    out("exshell 0.1, Phase 12.2\n");
    return 0;
}

static int builtin_help(int argc, char **argv);

static const struct builtin builtins[] = {
    { "echo",    "print arguments",                     builtin_echo    },
    { "help",    "list builtins, or 'help <name>'",     builtin_help    },
    { "version", "print shell version",                 builtin_version },
};

#define BUILTIN_COUNT ((int)(sizeof(builtins) / sizeof(builtins[0])))

static const struct builtin *find_builtin(const char *name) {
    for (int i = 0; i < BUILTIN_COUNT; ++i) {
        if (strcmp(builtins[i].name, name) == 0) return &builtins[i];
    }
    return (const struct builtin *)0;
}

static int builtin_help(int argc, char **argv) {
    if (argc >= 2) {
        const struct builtin *b = find_builtin(argv[1]);
        if (!b) {
            out(argv[1]);
            out(": no such builtin\n");
            return 1;
        }
        out(b->name);
        out(" - ");
        out(b->help);
        nl();
        return 0;
    }
    out("builtins:\n");
    for (int i = 0; i < BUILTIN_COUNT; ++i) {
        out("  ");
        out(builtins[i].name);
        out(" - ");
        out(builtins[i].help);
        nl();
    }
    out("  quit    exit the shell\n");
    out("  exit    alias for quit\n");
    return 0;
}

/* ---- tokenizer --------------------------------------------------- */

/* Split `line` in place on runs of spaces.  argv[] gets up to
 * ARGS_MAX NUL-terminated tokens; returns the count.  No quoting,
 * no escapes yet -- those land when the shell runs real commands. */
static int tokenize(char *line, char **argv, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) *p++ = '\0';
    }
    return n;
}

/* ---- dispatch ---------------------------------------------------- */

#define DISPATCH_QUIT (-1)

static int dispatch(int argc, char **argv) {
    if (argc == 0) return 0;

    if (strcmp(argv[0], "quit") == 0 || strcmp(argv[0], "exit") == 0)
        return DISPATCH_QUIT;

    const struct builtin *b = find_builtin(argv[0]);
    if (!b) {
        out(argv[0]);
        out(": command not found\n");
        return 127;
    }
    return b->fn(argc, argv);
}

/* ---- shell loop -------------------------------------------------- */

static int run_line(const char *src) {
    char buf[LINE_MAX];
    size_t n = strlen(src);
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, src, n);
    buf[n] = '\0';

    out("exyde> ");
    out(buf);
    nl();

    char *argv[ARGS_MAX];
    int argc = tokenize(buf, argv, ARGS_MAX);
    return dispatch(argc, argv);
}

/* The Phase 12.2 build runs a fixed script.  Interactive mode is
 * deferred to Phase 13 (keyboard driver server). */
static const char *const script[] = {
    "help",
    "echo hello, exshell",
    "echo Exyde microkernel",
    "version",
    "help echo",
    "echo unknown-cmd",
    "quit",
    (const char *)0,
};

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    out("exshell 0.1 (non-interactive build, Phase 12.2)\n");
    out("type 'help' for the builtin list\n");

    for (int i = 0; script[i] != (const char *)0; ++i) {
        int rc = run_line(script[i]);
        if (rc == DISPATCH_QUIT) {
            out("exshell: bye\n");
            return 0;
        }
    }
    return 0;
}
