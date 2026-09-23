; void gdt_flush(struct gdt_ptr *ptr);
;   rdi = pointer to { u16 limit; u64 base; }
;
; Loads the new GDT, reloads CS via a far return, reloads data segment
; registers, and loads the TSS selector into TR.
;
; GDT layout (see gdt.c):
;   0x00 null
;   0x08 kernel code
;   0x10 kernel data
;   0x18 user code   (RPL=3 -> 0x1B)
;   0x20 user data   (RPL=3 -> 0x23)
;   0x28 TSS low + 0x30 TSS high

BITS 64
section .text
global gdt_flush

gdt_flush:
    lgdt [rdi]

    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Load TSS selector (GDT index 5 => 0x28).
    mov ax, 0x28
    ltr ax

    ret
