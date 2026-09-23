#include <exyde/timer.h>
#include <arch/x86_64/io.h>

#define PIT_CH0   0x40u
#define PIT_CMD   0x43u
#define PIT_FREQ  1193182u

void timer_init(u32 hz) {
    if (hz == 0) {
        hz = 100;
    }

    u32 divisor = PIT_FREQ / hz;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;
    if (divisor < 1u)      divisor = 1u;

    /* Channel 0, lobyte/hibyte access, mode 3 (square wave), binary. */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (u8)(divisor & 0xFFu));
    outb(PIT_CH0, (u8)((divisor >> 8) & 0xFFu));
}
