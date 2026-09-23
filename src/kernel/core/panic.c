#include <exyde/console.h>
#include <exyde/panic.h>

void panic(const char *msg) {
    console_write("\n*** KERNEL PANIC ***\n");
    console_write(msg);
    console_write("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
