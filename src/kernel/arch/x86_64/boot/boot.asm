; ============================================================================
; Exyde x86_64 bootstrap.
;
; GRUB (Multiboot2) enters us in 32-bit protected mode with paging disabled.
; We build a minimal identity map for the first 1 GiB using 2 MiB pages,
; enable PAE + long mode + paging, far-jump into 64-bit code, set up the
; stack and call kmain(magic, mb_info_addr).
;
; No C runtime is assumed.
; ============================================================================

BITS 32

; ---------------------------------------------------------------------------
; Multiboot2 header.  Must be 8-byte aligned and inside the first 32 KiB
; of the file.  The linker script places .multiboot2 first.
; ---------------------------------------------------------------------------
section .multiboot2 progbits alloc noexec nowrite
align 8
mb2_start:
    dd 0xE85250D6                                    ; magic
    dd 0                                             ; arch: i386 (protected mode)
    dd mb2_end - mb2_start                           ; header length
    dd -(0xE85250D6 + 0 + (mb2_end - mb2_start))     ; checksum

    align 8
    dw 0                                             ; end tag: type
    dw 0                                             ; end tag: flags
    dd 8                                             ; end tag: size
mb2_end:

; ---------------------------------------------------------------------------
; 32-bit entry point.
; ---------------------------------------------------------------------------
section .bootstrap progbits alloc exec nowrite
global _start
extern kmain

_start:
    cli
    mov esp, stack32_top

    ; Save Multiboot2 registers passed by GRUB.
    mov [mb_magic], eax
    mov [mb_info],  ebx

    ; --- Clear page tables ------------------------------------------------
    mov edi, __boot_pml4
    xor eax, eax
    mov ecx, (4096 * 3) / 4
    rep stosd

    ; --- PML4[0] -> PDPT ---------------------------------------------------
    mov eax, __boot_pdpt
    or  eax, 0x3                                     ; present | writable
    mov [__boot_pml4], eax

    ; --- PDPT[0] -> PD -----------------------------------------------------
    mov eax, __boot_pd
    or  eax, 0x3
    mov [__boot_pdpt], eax

    ; --- PD: identity-map the first 1 GiB using 2 MiB pages ---------------
    mov edi, __boot_pd
    mov eax, 0x83                                    ; present | writable | 2MiB
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .fill_pd

    ; --- Enable PAE --------------------------------------------------------
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; --- Load PML4 into CR3 ------------------------------------------------
    mov eax, __boot_pml4
    mov cr3, eax

    ; --- Set EFER.LME ------------------------------------------------------
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; --- Enable paging (activates long mode) -------------------------------
    mov eax, cr0
    or  eax, (1 << 31)
    mov cr0, eax

    ; --- Load 64-bit GDT and far-jump into long mode -----------------------
    lgdt [gdt64_ptr]
    jmp 0x08:long_mode_entry


BITS 64

long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack64_top

    mov edi, [rel mb_magic]
    mov esi, [rel mb_info]

    call kmain

.hang:
    cli
    hlt
    jmp .hang

; ---------------------------------------------------------------------------
; Boot-time data.
; ---------------------------------------------------------------------------
section .boot.data progbits alloc noexec write
align 16
mb_magic:   dd 0
mb_info:    dd 0

section .boot.bss nobits alloc noexec write
align 4096
global __boot_pml4
global __boot_pdpt
global __boot_pd
__boot_pml4: resb 4096
__boot_pdpt: resb 4096
__boot_pd:   resb 4096

align 16
stack32_bottom: resb 4096
stack32_top:

align 16
stack64_bottom: resb 16384
stack64_top:

; ---------------------------------------------------------------------------
; 64-bit GDT.
; ---------------------------------------------------------------------------
section .rodata progbits alloc noexec nowrite
align 8
gdt64:
    dq 0x0000000000000000       ; null descriptor
    dq 0x00AF9A000000FFFF       ; 0x08: 64-bit kernel code (L=1, D=0)
    dq 0x00CF92000000FFFF       ; 0x10: data (D=1)
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dd gdt64                    ; 32-bit base, consumed by 32-bit lgdt
