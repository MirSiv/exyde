/* exshell -- Exyde shell, Phase 12.5.
 *
 * Non-interactive build: stdin still does not work reliably from a
 * WSL tty via QEMU -serial stdio (see EXYDE_PHASES.md 12.1), so
 * instead of read(0) the shell walks a fixed script through the
 * same parse/dispatch path that an interactive shell will use.
 * Phase 13 lands a keyboard driver server; switching to interactive
 * is then a three-line change (replace the script loop with
 * read_line()).
 *
 * 12.3b adds the filesystem builtins on top of the multiplexed
 * VFS from 12.3a:
 *
 *   pwd    cd    ls    cat    mkdir    rmdir    rm    touch
 *
 * plus a minimal '>' redirection for echo, used by the test script
 * (and by anyone wanting to put text in a file without a separate
 * editor).  Relative paths are resolved against the shell's own
 * cwd; there is no chdir syscall, the cwd lives in userspace and
 * every VFS request carries an absolute path.  The kernel knows
 * nothing about it.
 *
 * 12.5 adds environment builtins on top of libc/env.c:
 *
 *   env         print all NAME=VALUE pairs
 *   printenv    print one variable (or everything if no arg)
 *   export      setenv(name, value, overwrite=1); accepts NAME=VALUE
 *   unset       unsetenv(name)
 *
 * All userspace; libc/env.c owns the array and the ownership rules
 * for strings that came from the initial envp. */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <exyde/micro.h>
#include <exyde/vfs_client.h>

#define LINE_MAX   256
#define ARGS_MAX    16
#define PATH_MAX_  256

/* VFS open flags (mirror vfs_internal.h). */
#define O_RDONLY   0x0001u
#define O_WRONLY   0x0002u
#define O_CREAT    0x0100u
#define O_TRUNC    0x0400u

/* ---- shell state ------------------------------------------------- */

static char cwd[PATH_MAX_] = "/";

/* ---- I/O helpers ------------------------------------------------- */

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void nl(void) {
    write(1, "\n", 1);
}

/* ---- path resolution --------------------------------------------- */

/* Collapse '.' and '..' segments, drop empty ones, ensure a leading
 * slash.  `/a/b/../c` -> `/a/c`, `/..` -> `/`, `//x` -> `/x`. */
static int normalize_path(const char *in, char *outv, size_t cap) {
    static char buf[PATH_MAX_ * 2];
    size_t l = strlen(in);
    if (l >= sizeof buf) return -1;
    memcpy(buf, in, l + 1);

    const char *stack[32];
    int depth = 0;

    char *p = buf;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') ++p;
        if (*p) *p++ = '\0';

        if (strcmp(start, ".") == 0) continue;
        if (strcmp(start, "..") == 0) {
            if (depth > 0) depth--;
            continue;
        }
        if (depth >= 32) return -1;
        stack[depth++] = start;
    }

    size_t o = 0;
    if (cap < 2) return -1;
    outv[o++] = '/';
    for (int i = 0; i < depth; ++i) {
        size_t sl = strlen(stack[i]);
        if (o + sl + 1 >= cap) return -1;
        memcpy(outv + o, stack[i], sl); o += sl;
        outv[o++] = '/';
    }
    if (o > 1) o--;              /* strip trailing '/' unless root */
    outv[o] = '\0';
    return 0;
}

/* Turn `rel` into an absolute path by prepending cwd unless it is
 * already absolute.  Result is normalized. */
static int resolve_path(const char *rel, char *outv, size_t cap) {
    char tmp[PATH_MAX_ * 2];
    if (rel[0] == '/') {
        if (strlen(rel) >= sizeof tmp) return -1;
        strcpy(tmp, rel);
    } else {
        size_t cl = strlen(cwd);
        size_t rl = strlen(rel);
        if (cl + 1 + rl >= sizeof tmp) return -1;
        memcpy(tmp, cwd, cl);
        if (cl == 0 || tmp[cl - 1] != '/') tmp[cl++] = '/';
        memcpy(tmp + cl, rel, rl + 1);
    }
    return normalize_path(tmp, outv, cap);
}

/* ---- builtins: core ---------------------------------------------- */

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
    out("exshell 0.1, Phase 12.5\n");
    return 0;
}

static int builtin_help(int argc, char **argv);

/* ---- builtins: filesystem ---------------------------------------- */

static int builtin_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    out(cwd);
    nl();
    return 0;
}

static int builtin_cd(int argc, char **argv) {
    if (argc < 2) {
        strcpy(cwd, "/");
        return 0;
    }
    char p[PATH_MAX_];
    if (resolve_path(argv[1], p, sizeof p) != 0) {
        out("cd: path too long\n");
        return 1;
    }
    int fd = vfs_client_open(p, O_RDONLY, 0);
    if (fd < 0) {
        out("cd: "); out(p); out(": "); out(strerror(errno)); nl();
        return 1;
    }
    /* Distinguish directory from regular file by trying readdir at
     * index 0: empty dir -> ENOENT, populated dir -> success,
     * regular file -> ENOTDIR. */
    char name[128];
    errno = 0;
    int r = vfs_client_readdir(fd, 0, name, sizeof name);
    if (r != 0 && errno != ENOENT) {
        out("cd: "); out(p); out(": not a directory\n");
        vfs_client_close(fd);
        return 1;
    }
    vfs_client_close(fd);
    strcpy(cwd, p);
    return 0;
}

static int builtin_ls(int argc, char **argv) {
    const char *arg = (argc >= 2) ? argv[1] : ".";
    char p[PATH_MAX_];
    if (resolve_path(arg, p, sizeof p) != 0) {
        out("ls: path too long\n");
        return 1;
    }
    int fd = vfs_client_open(p, O_RDONLY, 0);
    if (fd < 0) {
        out("ls: "); out(p); out(": "); out(strerror(errno)); nl();
        return 1;
    }
    char name[128];
    for (uint32_t i = 0; ; ++i) {
        errno = 0;
        if (vfs_client_readdir(fd, i, name, sizeof name) != 0) {
            if (errno == ENOENT) break;
            out("ls: readdir error\n");
            vfs_client_close(fd);
            return 1;
        }
        out(name);
        nl();
    }
    vfs_client_close(fd);
    return 0;
}

static int builtin_cat(int argc, char **argv) {
    if (argc < 2) { out("cat: missing file\n"); return 1; }
    char p[PATH_MAX_];
    if (resolve_path(argv[1], p, sizeof p) != 0) {
        out("cat: path too long\n");
        return 1;
    }
    int fd = vfs_client_open(p, O_RDONLY, 0);
    if (fd < 0) {
        out("cat: "); out(p); out(": "); out(strerror(errno)); nl();
        return 1;
    }
    char buf[512];
    for (;;) {
        long n = vfs_client_read(fd, buf, sizeof buf);
        if (n < 0) {
            out("cat: read error\n");
            vfs_client_close(fd);
            return 1;
        }
        if (n == 0) break;
        if (write(1, buf, (size_t)n) != n) {
            vfs_client_close(fd);
            return 1;
        }
    }
    vfs_client_close(fd);
    return 0;
}

static int builtin_mkdir(int argc, char **argv) {
    if (argc < 2) { out("mkdir: missing path\n"); return 1; }
    char p[PATH_MAX_];
    if (resolve_path(argv[1], p, sizeof p) != 0) {
        out("mkdir: path too long\n"); return 1;
    }
    if (vfs_client_mkdir(p, 0755) != 0) {
        out("mkdir: "); out(p); out(": "); out(strerror(errno)); nl();
        return 1;
    }
    return 0;
}

static int builtin_rmdir(int argc, char **argv) {
    if (argc < 2) { out("rmdir: missing path\n"); return 1; }
    char p[PATH_MAX_];
    if (resolve_path(argv[1], p, sizeof p) != 0) {
        out("rmdir: path too long\n"); return 1;
    }
    if (vfs_client_rmdir(p) != 0) {
        out("rmdir: "); out(p); out(": "); out(strerror(errno)); nl();
        return 1;
    }
    return 0;
}

static int builtin_rm(int argc, char **argv) {
    if (argc < 2) { out("rm: missing path\n"); return 1; }
    char p[PATH_MAX_];
    if (resolve_path(argv[1], p, sizeof p) != 0) {
        out("rm: path too long\n"); return 1;
    }
    if (vfs_client_unlink(p) != 0) {
        out("rm: "); out(p); out(": "); out(strerror(errno)); nl();
        return 1;
    }
    return 0;
}

static int builtin_touch(int argc, char **argv) {
    if (argc < 2) { out("touch: missing file\n"); return 1; }
    char p[PATH_MAX_];
    if (resolve_path(argv[1], p, sizeof p) != 0) {
        out("touch: path too long\n"); return 1;
    }
    int fd = vfs_client_open(p, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        out("touch: "); out(p); out(": "); out(strerror(errno)); nl();
        return 1;
    }
    vfs_client_close(fd);
    return 0;
}

static int builtin_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    struct exyde_proc_info info[64];
    int n = exyde_proc_list(info, 64);
    if (n < 0) {
        out("ps: "); out(strerror(errno)); nl();
        return 1;
    }
    out("  PID  STATE  NAME\n");
    for (int i = 0; i < n; ++i) {
        char line[96];
        snprintf(line, sizeof line, "  %-4llu %-6s %s\n",
                 (unsigned long long)info[i].pid,
                 info[i].state ? "exit" : "run",
                 info[i].name);
        out(line);
    }
    return 0;
}

/* ---- builtins: environment --------------------------------------- */

static int builtin_env(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!environ) return 0;
    for (char **e = environ; *e; ++e) {
        out(*e);
        nl();
    }
    return 0;
}

static int builtin_printenv(int argc, char **argv) {
    if (argc < 2) return builtin_env(0, (char **)0);
    const char *v = getenv(argv[1]);
    if (!v) return 1;
    out(v);
    nl();
    return 0;
}

static int builtin_export(int argc, char **argv) {
    if (argc < 2) { out("export: missing argument\n"); return 1; }
    for (int i = 1; i < argc; ++i) {
        const char *eq = strchr(argv[i], '=');
        if (!eq) {
            /* `export NAME` without '=' is POSIX shorthand for
             * "mark an existing variable for export".  No separate
             * export table yet -- just report if it is not set. */
            if (!getenv(argv[i])) {
                out("export: "); out(argv[i]); out(": not set\n");
            }
            continue;
        }
        size_t nlen = (size_t)(eq - argv[i]);
        if (nlen == 0 || nlen >= 128) {
            out("export: invalid name\n");
            return 1;
        }
        char name[128];
        memcpy(name, argv[i], nlen);
        name[nlen] = '\0';
        if (setenv(name, eq + 1, 1) != 0) {
            out("export: "); out(name); out(": ");
            out(strerror(errno)); nl();
            return 1;
        }
    }
    return 0;
}

static int builtin_unset(int argc, char **argv) {
    if (argc < 2) { out("unset: missing argument\n"); return 1; }
    for (int i = 1; i < argc; ++i) {
        if (unsetenv(argv[i]) != 0) {
            out("unset: "); out(argv[i]); out(": ");
            out(strerror(errno)); nl();
            return 1;
        }
    }
    return 0;
}

/* ---- builtin table ----------------------------------------------- */

static const struct builtin builtins[] = {
    { "echo",    "print arguments",                     builtin_echo    },
    { "pwd",     "print working directory",             builtin_pwd     },
    { "cd",      "change directory",                    builtin_cd      },
    { "ls",      "list directory contents",             builtin_ls      },
    { "cat",     "print file contents",                 builtin_cat     },
    { "mkdir",   "create directory",                    builtin_mkdir   },
    { "touch",   "create empty file",                   builtin_touch   },
    { "rm",      "remove file",                         builtin_rm      },
    { "rmdir",   "remove empty directory",              builtin_rmdir   },
    { "ps",      "list processes",                      builtin_ps      },
    { "env",     "print all environment variables",     builtin_env     },
    { "printenv","print one environment variable",      builtin_printenv},
    { "export",  "set an environment variable",         builtin_export  },
    { "unset",   "remove an environment variable",      builtin_unset   },
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
            out(argv[1]); out(": no such builtin\n");
            return 1;
        }
        out(b->name); out(" - "); out(b->help); nl();
        return 0;
    }
    out("builtins:\n");
    for (int i = 0; i < BUILTIN_COUNT; ++i) {
        out("  "); out(builtins[i].name);
        out(" - "); out(builtins[i].help);
        nl();
    }
    out("  quit    exit the shell\n");
    out("  exit    alias for quit\n");
    out("  echo <text> > <file>   write text to file\n");
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

/* ---- echo with '>' redirect -------------------------------------- */

/* argv looks like: echo w0 w1 ... w(K-1) > file   with argv[K] == ">"
 * and argv[K+1] the path.  Writes the words joined by single spaces
 * plus a trailing newline. */
static int echo_to_file(int redir, int argc, char **argv) {
    (void)argc;
    char path[PATH_MAX_];
    if (resolve_path(argv[redir + 1], path, sizeof path) != 0) {
        out("echo: path too long\n");
        return 1;
    }
    int fd = vfs_client_open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        out("echo: "); out(path); out(": "); out(strerror(errno)); nl();
        return 1;
    }
    for (int i = 1; i < redir; ++i) {
        if (i > 1) {
            if (vfs_client_write(fd, " ", 1) != 1) {
                vfs_client_close(fd); out("echo: write failed\n"); return 1;
            }
        }
        size_t l = strlen(argv[i]);
        if (vfs_client_write(fd, argv[i], l) != (long)l) {
            vfs_client_close(fd); out("echo: write failed\n"); return 1;
        }
    }
    if (vfs_client_write(fd, "\n", 1) != 1) {
        vfs_client_close(fd); out("echo: write failed\n"); return 1;
    }
    vfs_client_close(fd);
    return 0;
}

/* ---- dispatch ---------------------------------------------------- */

#define DISPATCH_QUIT (-1)

static int dispatch(int argc, char **argv) {
    if (argc == 0) return 0;

    if (strcmp(argv[0], "quit") == 0 || strcmp(argv[0], "exit") == 0)
        return DISPATCH_QUIT;

    /* '>' redirection, currently only supported for echo (12.3b). */
    if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], ">") == 0) {
                if (i + 1 >= argc) {
                    out("echo: missing redirection target\n");
                    return 1;
                }
                return echo_to_file(i, argc, argv);
            }
        }
    }

    const struct builtin *b = find_builtin(argv[0]);
    if (!b) {
        out(argv[0]); out(": command not found\n");
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

    out("exyde> "); out(buf); nl();

    char *argv[ARGS_MAX];
    int argc = tokenize(buf, argv, ARGS_MAX);
    return dispatch(argc, argv);
}

/* The Phase 12.3b build runs a fixed script.  Interactive mode is
 * deferred to Phase 13 (keyboard driver server). */
static const char *const script[] = {
    "help",
    "pwd",
    "mkdir /tmp",
    "cd /tmp",
    "pwd",
    "touch notes.txt",
    "echo hello from exshell > notes.txt",
    "ls",
    "cat notes.txt",
    "echo second line overwrites > notes.txt",
    "cat notes.txt",
    "rm notes.txt",
    "ls",
    "cd /",
    "rmdir /tmp",
    "pwd",
    "env",
    "printenv PATH",
    "export EXYDE_TEST=hello",
    "printenv EXYDE_TEST",
    "unset EXYDE_TEST",
    "printenv EXYDE_TEST",
    "echo env tests done",
    "ps",
    "echo unknown-cmd",
    "quit",
    (const char *)0,
};

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    out("exshell 0.1 (non-interactive build, Phase 12.5)\n");
    out("type 'help' for the builtin list\n");

    /* Attach to the VFS server that init started.  If there is no
     * bootstrap capability (running standalone, say), skip the
     * filesystem builtins and continue with the rest. */
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
            out("exshell: bye\n");
            if (have_vfs) vfs_client_shutdown();
            return 0;
        }
    }
    if (have_vfs) vfs_client_shutdown();
    return 0;
}
