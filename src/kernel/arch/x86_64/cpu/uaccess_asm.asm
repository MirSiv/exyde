; Exyde x86_64 raw user<->kernel copy primitives.
;
; Contract (SysV AMD64):
;   int uaccess_memcpy_to_user  (void *udst,  const void *ksrc, size_t n);
;   int uaccess_memcpy_from_user(void *kdst,  const void *usrc, size_t n);
;   returns 0 on success, -EFAULT (== -14) if a #PF fires.
;
; The caller must have already switched CR3 to the target space.
; Only caller-saved registers are clobbered (rax, rsi, rdi, r8b).
; No prologue/epilogue is required -- the fault fixup relies on RSP
; still pointing at the caller's return address at the moment of the
; fault, so 'ret' in the fixup returns to C with rax = -EFAULT.
;
; A kernel-mode page fault on the user-side load or store is caught
; by the .extable mechanism: isr.c rewrites RIP to the matching
; fixup label, which sets rax = -EFAULT and returns to the C caller.

BITS 64

section .text

global uaccess_memcpy_to_user
uaccess_memcpy_to_user:
    ; rdi = user dst
    ; rsi = kernel src
    ; rdx = n
    mov     rax, rdx
    test    rax, rax
    jz      .done
.loop:
.trap_ld:
    mov     r8b, [rsi]              ; kernel src load (should not fault)
.trap_st:
    mov     [rdi], r8b              ; user dst store  (may fault)
    inc     rsi
    inc     rdi
    dec     rax
    jnz     .loop
.done:
    xor     eax, eax
    ret
.trap_fixup:
    mov     rax, -14
    ret

; Extable entries for uaccess_memcpy_to_user.
; Each entry is { fault_rip, fixup_rip } as two u64 values.
section .extable progbits alloc noexec nowrite
    dq      uaccess_memcpy_to_user.trap_ld
    dq      uaccess_memcpy_to_user.trap_fixup
    dq      uaccess_memcpy_to_user.trap_st
    dq      uaccess_memcpy_to_user.trap_fixup

section .text

global uaccess_memcpy_from_user
uaccess_memcpy_from_user:
    ; rdi = kernel dst
    ; rsi = user src
    ; rdx = n
    mov     rax, rdx
    test    rax, rax
    jz      .done
.loop:
.trap_ld:
    mov     r8b, [rsi]              ; user src load  (may fault)
.trap_st:
    mov     [rdi], r8b              ; kernel dst store (should not fault)
    inc     rsi
    inc     rdi
    dec     rax
    jnz     .loop
.done:
    xor     eax, eax
    ret
.trap_fixup:
    mov     rax, -14
    ret

section .extable progbits alloc noexec nowrite
    dq      uaccess_memcpy_from_user.trap_ld
    dq      uaccess_memcpy_from_user.trap_fixup
    dq      uaccess_memcpy_from_user.trap_st
    dq      uaccess_memcpy_from_user.trap_fixup
