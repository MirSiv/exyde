; ============================================================================
; x86_64 syscall entry.
;
; Layout of the GS-relative syscall_state struct (see syscall_arch.c):
;   [gs:0]  kernel_rsp
;   [gs:8]  scratch
;
; On entry: IF=0 (via FMASK), RCX=user RIP, R11=user RFLAGS,
;           RAX=nr, RDI=arg0, RSI=arg1, RDX=arg2, R10=arg3, R8=arg4.
; On exit:  RAX=result, RSP=user RSP, then SYSRETQ to user.
;
; Userspace relies on the Linux-style syscall ABI: every register
; except RCX, R11 and RAX is preserved across `syscall`.  syscall_dispatch
; is a C function and would otherwise clobber RDI/RSI/RDX/R10/R8/R9 as
; caller-saved, so we save/restore them explicitly here.
; ============================================================================

BITS 64
section .text

global syscall_entry
extern syscall_dispatch

syscall_entry:
    swapgs
    mov [gs:8], rsp
    mov rsp, [gs:0]

    ; --- save user registers (RAX = nr kept for the shuffle) ----------
    push qword [gs:8]      ; user RSP           (bottom of frame)
    push rcx               ; user RIP
    push r11               ; user RFLAGS
    push rax               ; nr
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    push rdi               ; a0
    push rsi               ; a1
    push rdx               ; a2
    push r10               ; a3
    push r8                ; a4
    push r9                ; scratch (preserve per ABI)  (top of frame)

    ; --- shuffle into C ABI -------------------------------------------
    ;   rdi = nr, rsi = a0, rdx = a1, rcx = a2, r8 = a3, r9 = a4
    mov r9, r8             ; r9 = a4
    mov r8, r10            ; r8 = a3
    mov rcx, rdx           ; rcx = a2
    mov rdx, rsi           ; rdx = a1
    mov rsi, rdi           ; rsi = a0
    mov rdi, rax           ; rdi = nr

    sti
    cld
    call syscall_dispatch

    cli

    ; --- restore user registers (RAX holds the return value) ----------
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    add rsp, 8             ; discard saved nr; RAX already has return
    pop r11                ; user RFLAGS (input to sysret)
    pop rcx                ; user RIP    (input to sysret)
    mov rsp, [rsp]         ; restore user RSP from top of frame

    swapgs
    o64 sysret
