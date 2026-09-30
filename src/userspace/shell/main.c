/* exshell -- Exyde shell, Phase 12.3a.
 *
 * Non-interactive build: stdin still does not work reliably from a
 * WSL tty via QEMU -serial stdio (see EXYDE_PHASES.md 12.1), so
 * instead of read(0) the shell walks a fixed script through the
 * same parse/dispatch path that an interactive shell will use.
 * Phase 13 lands a keyboard driver server; switching to interactive
 * is then a three-line change (replace the script loop with
 * read_line()).
 *
 * 12.3a: exshell attaches to the VFS server that init started, as
 * a second client.  init hands us its own VFS request channel as
 * the bootstrap capability; vfs_client_init creates a private
 * reply channel, sends ATTACH, and stores the assigned client_id.
 * A short VFS demo runs at the end of the fixed script.
 *
 * Architecture (kept deliberately close to a real shell):
 *
 *     line  ->  tokenize()  ->  argv[]  ->  dispatch()  ->  builtin
 *
 * Filesystem builtins proper land in 12.3b. */

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <exyde/micro.h>
#include <exyde/vfs_client.h>

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
    out("exshell 0.1, Phase 12.3a\n");
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

/* The Phase 12.3a build runs a fixed script.  Interactive mode is
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

/* 12.3a: exercise the second-client VFS attach.  Real filesystem
 * builtins (ls / cat / mkdir / rm / cd) land in 12.3b. */
static void vfs_demo(void) {
    out("exshell: VFS demo\n");

    if (vfs_client_mkdir("/shell-test", 0755) != 0) {
        out("exshell:   mkdir /shell-test FAILED\n");
        return;
    }
    out("exshell:   mkdir /shell-test ok\n");

    int fd = vfs_client_open("/shell-test/hello",
                             0x0102 /* O_CREAT|O_WRONLY */, 0644);
    if (fd < 0) {
        out("exshell:   open(w) FAILED\n");
        return;
    }
    const char *msg = "from exshell";
    if (vfs_client_write(fd, msg, 12) != 12) {
        out("exshell:   write FAILED\n");
        vfs_client_close(fd);
        return;
    }
    vfs_client_close(fd);
    out("exshell:   wrote /shell-test/hello\n");

    fd = vfs_client_open("/shell-test/hello", 0x0001 /* O_RDONLY */, 0);
    if (fd < 0) {
        out("exshell:   open(r) FAILED\n");
        return;
    }
    char buf[32];
    long n = vfs_client_read(fd, buf, 12);
    vfs_client_close(fd);
    if (n != 12) {
        out("exshell:   read FAILED\n");
        return;
    }
    buf[12] = 0;
    if (strcmp(buf, "from exshell") != 0) {
        out("exshell:   content mismatch\n");
        return;
    }
    out("exshell:   read back ok\n");

    vfs_client_unlink("/shell-test/hello");
    vfs_client_rmdir("/shell-test");
    out("exshell:   cleaned up\n");
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    out("exshell 0.1 (non-interactive build, Phase 12.3a)\n");
    out("type 'help' for the builtin list\n");

    /* Attach to the VFS server that init started.  If there is no
     * bootstrap capability (running standalone, say), skip the VFS
     * demo and continue with builtins only. */
    int have_vfs = 0;
    exyde_handle_t ch_vfs = exyde_get_bootstrap();
    if (ch_vfs != EXYDE_HANDLE_INVALID) {
        if (vfs_client_init(ch_vfs) == 0) {
            out("exshell: VFS client attached\n");
            have_vfs = 1;
        } else {
            out("exshell: vfs_client_init failed\n");
        }
    } else {
        out("exshell: no VFS bootstrap, running standalone\n");
    }

    for (int i = 0; script[i] != (const char *)0; ++i) {
        int rc = run_line(script[i]);
        if (rc == DISPATCH_QUIT) {
            if (have_vfs) vfs_demo();
            out("exshell: bye\n");
            if (have_vfs) vfs_client_shutdown();
            return 0;
        }
    }
    if (have_vfs) vfs_client_shutdown();
    return 0;
}
