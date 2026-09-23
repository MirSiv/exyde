; ============================================================================
; x86_64 syscall entry.
;
; Layout of the GS-relative syscall_state struct (see syscall_arch.c):
;   [gs:0]  kernel_rsp
;   [gs:8]  scratch
;
; On entry: IF=0 (via FMASK), RCX=user RIP, R11=user RFLAGS,
;           RAX=nr, RDI=arg0, RSI=arg1, RDX=arg2, R10=arg3, R8=arg4.
; On exit:  RAX=result, then SYSRETQ to user.
; ============================================================================

BITS 64
section .text

global syscall_entry
extern syscall_dispatch

syscall_entry:
    swapgs
    mov [gs:8], rsp
    mov rsp, [gs:0]

    push qword [gs:8]
    push rcx
    push r11
    push rax
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Shuffle args into C ABI: rdi=nr, rsi=a0, rdx=a1, rcx=a2, r8=a3, r9=a4
    mov r9, r8
    mov r8, r10
    mov r10, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, [rsp + 48]
    mov rcx, r10

    sti
    cld
    call syscall_dispatch

    cli
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    add rsp, 8
    pop r11
    pop rcx
    mov rsp, [rsp]
    swapgs
    o64 sysret
