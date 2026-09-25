#include <exyde/types.h>
#include <exyde/console.h>
#include <exyde/panic.h>
#include <exyde/arch.h>
#include <exyde/irq.h>
#include <exyde/timer.h>
#include <exyde/bootinfo.h>
#include <exyde/memmap.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
#include <exyde/heap.h>
#include <exyde/thread.h>
#include <exyde/sched.h>
#include <exyde/process.h>
#include <exyde/user.h>
#include <exyde/syscall.h>
#include <exyde/errno.h>
#include <exyde/handle.h>
#include <exyde/mutex.h>
#include <exyde/semaphore.h>
#include <exyde/condvar.h>
#include <exyde/channel.h>
#include <exyde/condev.h>
#include <exyde/exec.h>
#include <exyde/init_elf.h>
#include <exyde/elf_table.h>
#include <exyde/uaccess.h>
#include <exyde/vfs.h>
#include <exyde/ramfs.h>
#include <exyde/fd.h>
#include <exyde/uaccess.h>

extern char __bss_start[];
extern char __bss_end[];
extern char __kernel_start[];
extern char __kernel_end[];

static void zero_bss(void) {
    for (char *p = __bss_start; p < __bss_end; ++p) *p = '\0';
}

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289u
#define TIMER_HZ 100u

static u64 tick_count = 0;

static void timer_tick(u8 irq) {
    (void)irq;
    tick_count++;
    if ((tick_count % TIMER_HZ) == 0) {
        console_write("tick ");
        console_write_hex(tick_count);
        console_write("\n");
    }
    sched_tick();
}

static bool paddr_in_range(paddr_t p, paddr_t base, paddr_t end) {
    return p >= base && p < end;
}

static bool page_is_in_any_reserved(paddr_t p) {
    for (size_t i = 0; i < memmap_count(); ++i) {
        const memory_region_t *r = memmap_get(i);
        if (r->type == MEMORY_USABLE) continue;
        if (paddr_in_range(p, r->base, r->base + r->size)) return true;
    }
    return false;
}

static void pmm_selftest(void) {
    paddr_t p = pmm_alloc_page();
    if (p == 0) panic("pmm test: alloc_page returned 0");
    if (p & (paddr_t)(PAGE_SIZE - 1)) panic("pmm test: unaligned page");
    if (page_is_in_any_reserved(p)) panic("pmm test: page in reserved");
    if (!pmm_free_page(p)) panic("pmm test: free_page failed");
    if (pmm_free_page(p))  panic("pmm test: double free accepted");
    if (pmm_free_page(0))  panic("pmm test: free(0) accepted");
    paddr_t c = pmm_alloc_pages(8);
    if (c == 0) panic("pmm test: alloc_pages(8) failed");
    if (!pmm_free_pages(c, 8)) panic("pmm test: free_pages failed");
    console_ok("pmm test: OK\n");
}

static void vmm_selftest(void) {
    vmm_space_t ks = vmm_kernel_space();
    if (!ks) panic("vmm test: no space");
    paddr_t p = pmm_alloc_page();
    vaddr_t va = KERNEL_VA_BASE + 0x1000;
    if (!vmm_map(ks, va, p, VM_PRESENT | VM_WRITE)) panic("vmm: map failed");
    volatile u64 *slot = (volatile u64 *)va;
    *slot = 0x1122334455667788ULL;
    if (*slot != 0x1122334455667788ULL) panic("vmm: readback");
    if (!vmm_unmap(ks, va)) panic("vmm: unmap failed");
    pmm_free_page(p);
    console_ok("vmm test: OK\n");
}

static void heap_selftest(void) {
    u8 *a = (u8 *)exy_malloc(16);
    if (!a) panic("heap test: exy_malloc failed");
    if (((u64)a & 15) != 0) panic("heap test: unaligned");
    u8 *b = (u8 *)exy_zalloc(64);
    if (!b) panic("heap test: exy_zalloc failed");
    for (int i = 0; i < 64; ++i) if (b[i] != 0) panic("heap test: exy_zalloc nonzero");
    exy_free(a); exy_free(b);
    console_ok("heap test: OK\n");
}

static void handle_selftest(void) {
    handle_table_t t;
    handle_table_init(&t, (handle_release_fn)0);

    handle_t h1 = handle_create(&t, HANDLE_KIND_TEST, HANDLE_RIGHT_READ, (void *)0x1234);
    handle_t h2 = handle_create(&t, HANDLE_KIND_TEST, HANDLE_RIGHT_READ | HANDLE_RIGHT_WRITE, (void *)0x5678);
    if (h1 == HANDLE_INVALID || h2 == HANDLE_INVALID) panic("handle: create failed");
    if (h1 == h2) panic("handle: duplicate handle");

    void *obj = (void *)0;
    u32 kind = 0;
    if (!handle_lookup(&t, h1, HANDLE_RIGHT_READ, &obj, &kind)) panic("handle: lookup r failed");
    if (obj != (void *)0x1234) panic("handle: wrong object");
    if (handle_lookup(&t, h1, HANDLE_RIGHT_WRITE, &obj, &kind)) panic("handle: over-permission accepted");
    if (!handle_lookup(&t, h2, HANDLE_RIGHT_WRITE, &obj, &kind)) panic("handle: rw lookup failed");
    if (!handle_close(&t, h1)) panic("handle: close failed");
    if (handle_close(&t, h1))  panic("handle: double close accepted");
    if (handle_lookup(&t, h1, HANDLE_RIGHT_READ, &obj, &kind)) panic("handle: lookup after close");
    handle_close(&t, h2);
    console_ok("handle test: OK (create, lookup, rights, close)\n");
}

static volatile int worker_done = 0;

static void worker_thread(void *arg) {
    u64 id = (u64)arg;
    console_write("  worker ");
    console_write_hex(id);
    console_write(" ran\n");
    sched_yield();
    worker_done++;
}

static void sched_selftest(void) {
    thread_t *w1 = thread_create(worker_thread, (void *)1, "w1");
    thread_t *w2 = thread_create(worker_thread, (void *)2, "w2");
    if (!w1 || !w2) panic("sched test: create failed");
    for (int i = 0; i < 10000; ++i) {
        sched_yield();
        if (worker_done >= 2) break;
    }
    if (worker_done < 2) panic("sched test: workers not finished");
    console_ok("sched test: OK\n");
}

/* ---- Phase 8.1 synchronization self-test ----------------------------- */

#define SYNC_ITERS 10000

static mutex_t      mx;
static semaphore_t  sm;
static condvar_t    cv;
static mutex_t      cv_m;

static volatile u64 mx_counter;
static volatile u64 sm_counter;
static volatile int sync_done;
static volatile int cv_flag;
static volatile int cv_waiter_done;
static volatile int cv_signaler_done;

static void mx_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < SYNC_ITERS; ++i) {
        mutex_lock(&mx);
        mx_counter++;
        if ((i & 31) == 0) sched_yield();
        mutex_unlock(&mx);
    }
    __atomic_add_fetch(&sync_done, 1, __ATOMIC_SEQ_CST);
}

static void sm_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < SYNC_ITERS; ++i) {
        sem_wait(&sm);
        sm_counter++;
        if ((i & 31) == 0) sched_yield();
        sem_post(&sm);
    }
    __atomic_add_fetch(&sync_done, 1, __ATOMIC_SEQ_CST);
}

static void cv_waiter(void *arg) {
    (void)arg;
    mutex_lock(&cv_m);
    while (!cv_flag) cond_wait(&cv, &cv_m);
    mutex_unlock(&cv_m);
    cv_waiter_done = 1;
}

static void cv_signaler(void *arg) {
    (void)arg;
    for (volatile int i = 0; i < 2000000; ++i) { /* delay */ }
    mutex_lock(&cv_m);
    cv_flag = 1;
    cond_signal(&cv);
    mutex_unlock(&cv_m);
    cv_signaler_done = 1;
}

/* ---- Phase 8.2 channel self-test -------------------------------------- */

#define CHAN_MSGS 200

static channel_t *test_chan;
static volatile int chan_producer_done;
static volatile int chan_consumer_done;
static volatile u64 chan_sum_sent;
static volatile u64 chan_sum_recv;
static volatile int chan_order_ok;

static void chan_producer(void *arg) {
    (void)arg;
    for (u64 i = 0; i < CHAN_MSGS; ++i) {
        u64 msg = i * 3 + 1;
        chan_sum_sent += msg;
        if (!channel_send(test_chan, &msg)) break;
    }
    chan_producer_done = 1;
}

static void chan_consumer(void *arg) {
    (void)arg;
    u64 expected = 1;
    int ok = 1;
    for (int i = 0; i < CHAN_MSGS; ++i) {
        u64 msg = 0;
        if (!channel_recv(test_chan, &msg)) { ok = 0; break; }
        if (msg != expected) ok = 0;
        expected += 3;
        chan_sum_recv += msg;
    }
    chan_order_ok = ok;
    chan_consumer_done = 1;
}

static void channel_selftest(void) {
    channel_t *c = channel_create(2, sizeof(u64));
    if (!c) panic("channel test: create failed");
    if (channel_count(c) != 0)    panic("channel test: initial count");
    if (channel_capacity(c) != 2) panic("channel test: capacity");

    u64 a = 111, b = 222, cval = 333, out = 0;
    if (!channel_try_send(c, &a))    panic("channel test: try_send 1");
    if (!channel_try_send(c, &b))    panic("channel test: try_send 2");
    if (channel_try_send(c, &cval))  panic("channel test: try_send on full accepted");
    if (channel_count(c) != 2)       panic("channel test: count after 2 sends");
    if (!channel_try_recv(c, &out) || out != 111) panic("channel test: try_recv 1");
    if (!channel_try_recv(c, &out) || out != 222) panic("channel test: try_recv 2");
    if (channel_try_recv(c, &out))   panic("channel test: try_recv on empty accepted");
    if (channel_count(c) != 0)       panic("channel test: count after recv");
    channel_destroy(c);

    test_chan = channel_create(4, sizeof(u64));
    if (!test_chan) panic("channel test: create 2 failed");
    chan_producer_done = 0;
    chan_consumer_done = 0;
    chan_sum_sent = 0;
    chan_sum_recv = 0;
    chan_order_ok = 1;

    if (!thread_create(chan_producer, (void *)0, "producer")) panic("channel: create producer");
    if (!thread_create(chan_consumer, (void *)0, "consumer")) panic("channel: create consumer");

    for (int i = 0; i < 500000; ++i) {
        sched_yield();
        if (chan_producer_done && chan_consumer_done) break;
    }
    if (!chan_producer_done) panic("channel test: producer hung");
    if (!chan_consumer_done) panic("channel test: consumer hung");
    if (!chan_order_ok)      panic("channel test: message order lost");
    if (chan_sum_sent != chan_sum_recv) panic("channel test: sum mismatch");
    if (channel_count(test_chan) != 0) panic("channel test: residual data in channel");

    channel_destroy(test_chan);
    test_chan = (channel_t *)0;

    console_ok("channel test: OK (try_send, try_recv, blocking, order, sum)\n");
}

static void sync_selftest(void) {
    /* --- mutex --- */
    mutex_init(&mx);
    mx_counter = 0;
    sync_done  = 0;

    if (!thread_create(mx_worker, (void *)0, "mx1")) panic("sync: create mx1");
    if (!thread_create(mx_worker, (void *)0, "mx2")) panic("sync: create mx2");

    while (sync_done < 2) sched_yield();

    if (mx_counter != (u64)(2 * SYNC_ITERS)) {
        console_write("mutex test: counter = ");
        console_write_hex(mx_counter);
        console_write(" expected ");
        console_write_hex((u64)(2 * SYNC_ITERS));
        console_write("\n");
        panic("sync test: mutex counter mismatch");
    }
    console_set_color(CONSOLE_COLOR_GREEN);
    console_write("mutex test: OK (");
    console_write_hex(mx_counter);
    console_write(" increments under contention)\n");
    console_reset_color();

    /* --- semaphore as binary mutex --- */
    sem_init(&sm, 1);
    sm_counter = 0;
    sync_done  = 0;

    if (!thread_create(sm_worker, (void *)0, "sm1")) panic("sync: create sm1");
    if (!thread_create(sm_worker, (void *)0, "sm2")) panic("sync: create sm2");

    while (sync_done < 2) sched_yield();

    if (sm_counter != (u64)(2 * SYNC_ITERS)) {
        console_write("sem test: counter = ");
        console_write_hex(sm_counter);
        console_write("\n");
        panic("sync test: sem counter mismatch");
    }
    console_set_color(CONSOLE_COLOR_GREEN);
    console_write("semaphore test: OK (");
    console_write_hex(sm_counter);
    console_write(" increments under contention)\n");
    console_reset_color();

    /* --- condvar --- */
    mutex_init(&cv_m);
    cond_init(&cv);
    cv_flag          = 0;
    cv_waiter_done   = 0;
    cv_signaler_done = 0;

    if (!thread_create(cv_waiter,   (void *)0, "cvw")) panic("sync: create cvw");
    if (!thread_create(cv_signaler, (void *)0, "cvs")) panic("sync: create cvs");

    for (int i = 0; i < 200000; ++i) {
        sched_yield();
        if (cv_waiter_done && cv_signaler_done) break;
    }
    if (!cv_waiter_done || !cv_signaler_done) {
        panic("sync test: condvar timeout");
    }
    console_ok("condvar test: OK (wait/signal handshake)\n");
}

/* ---- Test ELF: mov edi,0x42; mov eax,0; syscall; int3; hlt ------------ */

static const u8 test_elf_syscall[] = {
    0x7F, 'E', 'L', 'F',
    0x02, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x3E, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x38, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xBF, 0x42, 0x00, 0x00, 0x00,
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x05,
    0xCC,
    0xF4,
};

#define PING_ARG0       0x0000000000000042ULL
#define PING_EXPECTED   (0xDEADBEEFCAFE0000ULL ^ PING_ARG0)

static volatile int user_test_hit = 0;
static volatile u64 user_test_rax = 0;

static void user_fault_handler(const user_fault_t *f) {
    if (f->int_no == 3) {
        user_test_rax = f->rax;
        user_test_hit = 1;
        thread_exit();
    }
    console_write("  user: unexpected int_no=");
    console_write_hex(f->int_no);
    console_write(" rip=");
    console_write_hex(f->rip);
    console_write("\n");
    panic("ring3 test: unexpected user fault");
}

static void user_thread_entry(void *arg) {
    process_t *p = (process_t *)arg;
    arch_enter_user_mode(p->image.entry, USER_STACK_TOP);
}

static void ring3_syscall_selftest(void) {
    void *ballast = exy_malloc(256 * 1024);
    if (!ballast) panic("ring3 test: ballast alloc failed");
    exy_free(ballast);

    process_t *p = process_create_from_elf("r3", test_elf_syscall,
                                           sizeof(test_elf_syscall));
    if (!p) panic("ring3 test: create failed");

    paddr_t sp = pmm_alloc_page();
    if (!sp) panic("ring3 test: no stack page");
    if (!vmm_map(p->space, USER_STACK_TOP - PAGE_SIZE, sp,
                 VM_PRESENT | VM_WRITE | VM_USER)) {
        panic("ring3 test: stack map failed");
    }

    arch_set_user_fault_handler(user_fault_handler);

    user_test_hit = 0;
    user_test_rax = 0;

    /* Thread owns one reference to the space; process_destroy below
     * drops the process's reference. */
    vmm_space_ref(p->space);
    thread_t *t = thread_create_ex(user_thread_entry, p, "r3", p->space);
    if (!t) panic("ring3 test: thread create failed");
    t->process = p;

    for (int i = 0; i < 1000000; ++i) {
        if (user_test_hit) break;
        sched_yield();
    }
    if (!user_test_hit) panic("ring3 test: timeout");

    if (user_test_rax != PING_EXPECTED) {
        console_write("ring3 test: rax = ");
        console_write_hex(user_test_rax);
        console_write(" expected ");
        console_write_hex(PING_EXPECTED);
        console_write("\n");
        panic("ring3 test: sys_ping returned wrong value");
    }

    /* The r3 test does NOT attach the process to the thread as its
     * main_thread, so we own the process's primary reference.  The
     * thread has already exited (via user_fault_handler -> thread_exit)
     * and been queued for reaping, but it holds no process ref, so
     * the only way to release p is to call process_unref here.
     *
     * Page-count delta is not checked: with refcounting in place the
     * actual free happens inside process_unref, and the exact timing
     * relative to the zombie reaper is not deterministic.  The
     * userspace_selftest below performs the same leak check across a
     * full spawn/exit cycle. */
    process_unref(p);

    console_ok("ring3 test: OK (SYSCALL from CPL3, arg passing, ping round-trip)\n");
}

/* ---- Phase 9: VFS / RAMFS / fd self-test ----------------------------- */

static void vfs_selftest(void) {
    vnode_t *root = (vnode_t *)0;
    int r = vfs_resolve("/", &root);
    if (r < 0) panic("vfs test: resolve /");
    if (root->type != VFS_TYPE_DIR) panic("vfs test: / not a dir");
    vnode_unref(root);

    if (vfs_mkdir("/tmp", 0755) < 0) panic("vfs test: mkdir /tmp");

    file_t *f = (file_t *)0;
    if (vfs_open("/tmp/hello", VFS_O_CREAT | VFS_O_WRONLY, 0644, &f) < 0)
        panic("vfs test: open create");

    const char *msg = "hello exyde\n";
    i64 n = vfs_write(f, msg, 12);
    if (n != 12) panic("vfs test: write count");
    if (f->offset != 12) panic("vfs test: offset after write");
    vfs_close(f);

    if (vfs_open("/tmp/hello", VFS_O_RDONLY, 0, &f) < 0)
        panic("vfs test: open read");
    char buf[32];
    n = vfs_read(f, buf, sizeof(buf));
    if (n != 12) panic("vfs test: read count");
    for (int i = 0; i < 12; ++i)
        if (buf[i] != msg[i]) panic("vfs test: read content");
    vfs_close(f);

    if (vfs_open("/tmp/hello", VFS_O_RDONLY, 0, &f) < 0)
        panic("vfs test: open seek");
    if (vfs_seek(f, 6, VFS_SEEK_SET) != 6) panic("vfs test: seek SET");
    n = vfs_read(f, buf, 5);
    if (n != 5) panic("vfs test: read after seek");
    if (buf[0]!='e' || buf[1]!='x' || buf[2]!='y' || buf[3]!='d' || buf[4]!='e')
        panic("vfs test: content after seek");
    if (vfs_seek(f, -6, VFS_SEEK_END) != 6) panic("vfs test: seek END");
    vfs_close(f);

    r = vfs_open("/tmp/hello", VFS_O_CREAT | VFS_O_EXCL | VFS_O_WRONLY, 0644, &f);
    if (r != -EEXIST) panic("vfs test: O_EXCL accepted existing");

    vnode_t *tmp = (vnode_t *)0;
    if (vfs_resolve("/tmp", &tmp) < 0) panic("vfs test: resolve /tmp");
    char name[VFS_NAME_MAX + 1];
    if (tmp->ops->readdir(tmp, 0, name, sizeof(name)) < 0)
        panic("vfs test: readdir 0");
    if (name[0]!='h' || name[1]!='e' || name[2]!='l' ||
        name[3]!='l' || name[4]!='o' || name[5]!='\0')
        panic("vfs test: readdir content");
    if (tmp->ops->readdir(tmp, 1, name, sizeof(name)) != -ENOENT)
        panic("vfs test: readdir past end");
    vnode_unref(tmp);

    if (vfs_unlink("/tmp/hello") < 0) panic("vfs test: unlink");
    vnode_t *gone = (vnode_t *)0;
    if (vfs_resolve("/tmp/hello", &gone) != -ENOENT)
        panic("vfs test: resolve after unlink");

    file_t *inner = (file_t *)0;
    if (vfs_open("/tmp/inner", VFS_O_CREAT | VFS_O_WRONLY, 0644, &inner) < 0)
        panic("vfs test: create inner");
    vfs_close(inner);

    if (vfs_rmdir("/tmp") != -ENOTEMPTY)
        panic("vfs test: rmdir non-empty accepted");
    if (vfs_unlink("/tmp/inner") < 0) panic("vfs test: unlink inner");
    if (vfs_rmdir("/tmp") < 0)        panic("vfs test: rmdir empty");

    console_ok("vfs test: OK (mkdir, create, write, read, seek, readdir, unlink, rmdir)\n");
}

static void fd_selftest(void) {
    fd_table_t t;
    fd_table_init(&t);

    if (vfs_mkdir("/fdtest", 0755) < 0) panic("fd test: mkdir");

    file_t *a = (file_t *)0;
    if (vfs_open("/fdtest/a", VFS_O_CREAT | VFS_O_RDWR, 0644, &a) < 0)
        panic("fd test: create a");
    int fd_a = fd_alloc(&t, a);
    if (fd_a != 0) panic("fd test: first fd != 0");

    file_t *b = (file_t *)0;
    if (vfs_open("/fdtest/b", VFS_O_CREAT | VFS_O_RDWR, 0644, &b) < 0)
        panic("fd test: create b");
    int fd_b = fd_alloc(&t, b);
    if (fd_b != 1) panic("fd test: second fd != 1");

    if (fd_get(&t, 0) != a) panic("fd test: get 0");
    if (fd_get(&t, 1) != b) panic("fd test: get 1");
    if (fd_get(&t, 2) != (file_t *)0) panic("fd test: get 2 should be NULL");

    if (fd_close(&t, 0) != 0) panic("fd test: close 0");
    if (fd_get(&t, 0) != (file_t *)0) panic("fd test: get 0 after close");
    if (fd_close(&t, 0) != -EBADF) panic("fd test: double close accepted");

    file_t *c = (file_t *)0;
    if (vfs_open("/fdtest/c", VFS_O_CREAT | VFS_O_RDWR, 0644, &c) < 0)
        panic("fd test: create c");
    int fd_c = fd_alloc(&t, c);
    if (fd_c != 0) panic("fd test: lowest free not reused");

    fd_table_destroy(&t);

    if (vfs_unlink("/fdtest/a") < 0) panic("fd test: unlink a");
    if (vfs_unlink("/fdtest/b") < 0) panic("fd test: unlink b");
    if (vfs_unlink("/fdtest/c") < 0) panic("fd test: unlink c");
    if (vfs_rmdir("/fdtest") < 0) panic("fd test: rmdir");

    console_ok("fd test: OK (alloc, get, close, reuse lowest, table destroy)\n");
}

static void userspace_selftest(void) {
    const elf_entry_t *init_elf = elf_table_lookup("init");
    if (!init_elf || !init_elf->blob_start || elf_entry_size(init_elf) == 0)
        panic("userspace test: no init elf in table");

    const char *argv[] = { "init", (const char *)0 };
    const char *envp[] = { "PATH=/", (const char *)0 };
    int argc = 1;
    int envc = 1;

    u64 before = pmm_free_page_count();

    process_t *init = process_spawn("init",
                                    init_elf->blob_start,
                                    (size_t)elf_entry_size(init_elf),
                                    argc, argv, envc, envp);
    if (!init) panic("userspace test: spawn init failed");

    exy_printf("userspace: spawned init pid=%u, pmm free before=%u\n",
            (u32)init->pid, (u32)before);

    for (int i = 0; i < 1000; ++i) sched_yield();

    u64 after = pmm_free_page_count();
    exy_printf("userspace: after reaper, pmm free=%u\n", (u32)after);

    if (after < before)
        panic("userspace test: reaper leaked pages");

    console_ok("userspace test: OK (init spawned, ran, exited)\n");
}


static void uaccess_selftest(void) {
    vmm_space_t space = vmm_kernel_space();
    if (user_range_ok(space, USER_VA_BASE, 8) == 0)
        panic("uaccess test: kernel space accepted user range");
    if (user_range_ok(space, 0, 8) == 0)
        panic("uaccess test: NULL accepted");
    if (user_range_ok(space, USER_VA_TOP - 4, 8) == 0)
        panic("uaccess test: range crossing top accepted");
    if (user_range_ok(space, USER_VA_TOP, 8) == 0)
        panic("uaccess test: range above top accepted");
    if (user_range_ok(space, USER_VA_BASE, (size_t)-1) == 0)
        panic("uaccess test: wrap-around accepted");
    if (user_range_ok(space, USER_VA_BASE, 0) != 0)
        panic("uaccess test: zero-length rejected");

    /* Extable path: in the kernel address space, PML4[1] is absent,
     * so a direct deref at USER_VA_BASE traps with #PF.  The raw
     * uaccess primitives must catch it via .extable and return
     * -EFAULT without panicking.  This bypasses user_range_ok on
     * purpose -- we are unit-testing the fault-recovery mechanism,
     * i.e. the second half of the TOCTOU window. */
    {
        extern int uaccess_memcpy_from_user(void *kdst, const void *usrc, size_t n);
        extern int uaccess_memcpy_to_user  (void *udst, const void *ksrc, size_t n);
        char buf[8] = {0};
        if (uaccess_memcpy_from_user(buf, (const void *)USER_VA_BASE, 8) != -EFAULT)
            panic("uaccess test: extable from_user did not return -EFAULT");
        if (uaccess_memcpy_to_user((void *)USER_VA_BASE, buf, 8) != -EFAULT)
            panic("uaccess test: extable to_user did not return -EFAULT");
    }

    console_ok("uaccess test: OK (range bounds, wrap, zero-len, extable)\n");
}

void kmain(u32 magic, u64 mb_info_addr) {
    zero_bss();
    arch_init();

    console_banner("Exyde kernel: alive\n");

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) panic("invalid multiboot2 magic");
    console_write("multiboot2 magic OK\n");

    console_write("mb_info @ ");
    console_write_hex(mb_info_addr);
    console_write("\n");

    console_write("arch: console + gdt + idt + pic ready\n");

    bootinfo_init(mb_info_addr);
    memmap_reserve(0, 0x100000);
    memmap_reserve((paddr_t)__kernel_start,
                   (u64)(__kernel_end - __kernel_start));

    pmm_init();

    console_write("pmm: total ");
    console_write_hex((u64)pmm_total_page_count());
    console_write(" pages, free ");
    console_write_hex((u64)pmm_free_page_count());
    console_write("\n");

    pmm_selftest();
    vmm_selftest();

    heap_init();
    heap_selftest();

    handle_selftest();

    sched_init();

    irq_register(0, timer_tick);
    timer_init(TIMER_HZ);
    arch_irqs_enable();

    console_notice("timer: 100 Hz, preemptive\n");
    sched_selftest();
    sync_selftest();
    channel_selftest();
    vfs_init();
    vnode_t *ramfs_root = ramfs_create_root();
    if (!ramfs_root) panic("vfs: ramfs_create_root failed");
    vfs_mount_root(ramfs_root);
    vfs_selftest();
    fd_selftest();

    condev_init();
    if (!condev_get()) panic("condev init failed");

    syscall_arch_init();
    ring3_syscall_selftest();
    userspace_selftest();
    uaccess_selftest();

    console_notice("idle: entering hlt loop\n");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
