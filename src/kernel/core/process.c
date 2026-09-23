#include <exyde/process.h>
#include <exyde/heap.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
#include <exyde/panic.h>

static u64 next_pid = 1;

process_t *process_create_from_elf(const char *name,
                                   const void *elf, size_t size) {
    vmm_space_t space = vmm_create();
    if (!space) return (process_t *)0;

    elf_image_t img;
    if (!elf_load(space, elf, size, &img)) {
        vmm_space_unref(space);
        return (process_t *)0;
    }

    process_t *p = (process_t *)kzalloc(sizeof(process_t));
    if (!p) {
        vmm_space_unref(space);
        return (process_t *)0;
    }

    p->pid         = next_pid++;
    p->name        = name;
    p->space       = space;
    p->image       = img;
    p->main_thread = (thread_t *)0;
    p->initial_rsp = 0;
    handle_table_init(&p->handles);
    fd_table_init(&p->fds);

    return p;
}

void process_destroy(process_t *p) {
    if (!p) return;
    fd_table_destroy(&p->fds);
    vmm_space_unref(p->space);
    kfree(p);
}
