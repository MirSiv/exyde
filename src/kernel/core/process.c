#include <exyde/process.h>
#include <exyde/heap.h>
#include <exyde/pmm.h>
#include <exyde/vmm.h>
#include <exyde/channel.h>
#include <exyde/panic.h>

static u64 next_pid = 1;

/* Release kinds owned by the kernel for a dying process.  Only kinds
 * whose object is a kernel allocation appear here; HANDLE_KIND_TEST
 * and HANDLE_KIND_NONE own nothing. */
static void handle_release(u32 kind, void *object) {
    if (kind == HANDLE_KIND_CHANNEL) {
        channel_destroy((channel_t *)object);
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
    p->brk         = (vaddr_t)(img.load_base + img.load_size);

    /* Start the "kernel-chosen" MAP area at the first 64 KiB boundary
     * past the loaded image, so it never collides with text/data. */
    vaddr_t m = (vaddr_t)(img.load_base + img.load_size);
    p->next_map_va = (m + 0xFFFF) & ~((vaddr_t)0xFFFF);

    handle_table_init(&p->handles);
    fd_table_init(&p->fds);

    return p;
}

void process_destroy(process_t *p) {
    if (!p) return;
    handle_table_destroy(&p->handles, handle_release);
    fd_table_destroy(&p->fds);
    vmm_space_unref(p->space);
    kfree(p);
}
