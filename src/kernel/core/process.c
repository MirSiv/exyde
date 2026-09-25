#include <exyde/process.h>
#include <exyde/heap.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
#include <exyde/channel.h>
#include <exyde/handle.h>
#include <exyde/sched.h>
#include <exyde/arch.h>
#include <exyde/elf.h>
#include <exyde/panic.h>

static u64 next_pid = 1;

static void handle_release(u32 kind, void *object) {
    if (kind == HANDLE_KIND_CHANNEL) {
        channel_destroy((channel_t *)object);
    } else if (kind == HANDLE_KIND_PROCESS) {
        process_unref((process_t *)object);
    }
}

process_t *process_create_from_elf(const char *name,
                                   const void *elf, size_t size) {
    vmm_space_t space = vmm_create();
    if (!space) return (process_t *)0;

    elf_image_t img;
    if (!elf_load(space, elf, size, &img)) {
        vmm_space_unref(space);
        return (process_t *)0;
    }

    process_t *p = (process_t *)exy_zalloc(sizeof(process_t));
    if (!p) {
        vmm_space_unref(space);
        return (process_t *)0;
    }

    size_t name_len = 0;
    while (name && name[name_len]) ++name_len;
    name_len += 1;
    char *name_copy = (char *)exy_malloc(name_len);
    if (!name_copy) {
        vmm_space_unref(space);
        exy_free(p);
        return (process_t *)0;
    }
    for (size_t i = 0; i < name_len; ++i) name_copy[i] = name[i];

    p->pid              = next_pid++;
    p->name             = name_copy;
    p->space            = space;
    p->entry            = img.entry;
    p->main_thread      = (thread_t *)0;
    p->initial_rsp      = 0;

    vaddr_t m = (vaddr_t)(img.load_base + img.load_size);
    p->next_map_va      = (m + 0xFFFF) & ~((vaddr_t)0xFFFF);

    p->refcount         = 1;
    p->exit_code        = 0;
    p->exited           = 0;
    p->bootstrap_handle = HANDLE_INVALID;

    handle_table_init(&p->handles, handle_release);
    waitq_init(&p->waiters);

    return p;
}

void process_ref(process_t *p) {
    if (!p) return;
    p->refcount++;
}

void process_unref(process_t *p) {
    if (!p) return;
    if (p->refcount == 0) return;
    if (--p->refcount == 0) process_destroy(p);
}

void process_exit(process_t *p, int code) {
    if (!p) return;
    if (p->exited) return;

    u64 flags = arch_irqs_save_and_disable();
    p->exit_code = code;
    p->exited    = 1;

    thread_t *w;
    while ((w = waitq_pop(&p->waiters)) != (thread_t *)0) {
        sched_unblock(w);
    }
    arch_irqs_restore(flags);
}

void process_destroy(process_t *p) {
    if (!p) return;
    handle_table_destroy(&p->handles);
    vmm_space_unref(p->space);
    if (p->name) exy_free((void *)p->name);
    exy_free(p);
}
