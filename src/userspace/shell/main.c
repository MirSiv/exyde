/* exshell -- Exyde shell, Phase 12.1 (non-interactive stub).
 *
 * The original 12.1 plan was an interactive REPL: prompt, read a
 * line from stdin, echo it, exit on "quit"/"exit".  The REPL works
 * from the kernel's side but not from the host's: serial input
 * through a WSL tty plus QEMU `-serial stdio` does not deliver
 * keystrokes to COM1 reliably (the host tty is not put into raw
 * mode, local echo and line buffering interfere).  No amount of
 * kernel or libc work fixes a host-side tty problem.
 *
 * Phase 13 introduces a real userspace keyboard driver server, at
 * which point stdin is a proper device and the REPL loop comes back
 * with a three-line change (swap this fixed script for read_line()).
 * For now exshell runs a small deterministic script of builtins so
 * the build/initrd/init wiring is proven end-to-end. */

#include <unistd.h>
#include <string.h>

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void cmd_echo(const char *arg) {
    out("echo: ");
    out(arg);
    out("\n");
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    out("exshell 0.1 (non-interactive build, Phase 12.1)\n");

    out("exyde> echo hello\n");
    cmd_echo("hello");

    out("exyde> echo Exyde microkernel\n");
    cmd_echo("Exyde microkernel");

    out("exyde> version\n");
    out("exshell 0.1, Phase 12.1\n");

    out("exyde> quit\n");
    out("exshell: bye\n");
    return 0;
}
