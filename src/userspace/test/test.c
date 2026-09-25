/* test.elf -- Phase 11 libc/env/heap/micro regression suite.
 *
 * Phase 11.5.2 batch 1: placeholder.  The real tests currently live
 * in init/init.c and move here in batch 4.  Until then this binary
 * exists so the elf_table infrastructure is exercised end to end. */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    return 0;
}
