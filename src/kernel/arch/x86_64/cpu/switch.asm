; ============================================================================
; Exyde x86_64 context switch.
;
; thread_switch(u64 *from_rsp /rdi/, u64 *to_rsp /rsi/):
;   - push callee-saved registers onto the current stack
;   - store the resulting rsp into *from_rsp
;   - load *to_rsp into rsp
;   - pop callee-saved registers
;   - ret   (returns into whatever "rip" was on the new stack)
;
; A fresh thread's stack is prepared by thread_create() so that the ret
; lands in thread_trampoline with r12 = entry and r13 = arg.
; ============================================================================

BITS 64

section .text

global thread_switch
global thread_trampoline
extern thread_exit

thread_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov  [rdi], rsp
    mov  rsp, [rsi]
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

; First entry point of a freshly created thread.
; Entered via `ret` from thread_switch.
;   r12 = entry function
;   r13 = argument
thread_trampoline:
    sti
    mov rdi, r13
    call r12
    call thread_exit
.hang:
    cli
    hlt
    jmp .hang
