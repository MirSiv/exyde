; Exception stubs for vectors 0..31 and IRQ stubs for vectors 32..47.
;
; Stack layout after the stub has pushed int_no / err_code and all GPRs
; (from low address upward) matches struct regs in isr.h:
;
;   [rsp +   0]  r15
;   ...
;   [rsp + 112]  rax
;   [rsp + 120]  int_no
;   [rsp + 128]  err_code
;   [rsp + 136]  rip         (pushed by CPU)
;   [rsp + 144]  cs
;   [rsp + 152]  rflags
;   [rsp + 160]  rsp
;   [rsp + 168]  ss

BITS 64

extern isr_dispatch

section .text

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0        ; fake error code
    push qword %1       ; vector number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    ; CPU already pushed the error code.
    push qword %1
    jmp isr_common
%endmacro

; --- CPU exceptions 0..31 ---
; Vectors with an error code: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30.
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; --- PIC IRQs remapped to 32..47.  None carry an error code. ---
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr_dispatch

    ; Returns normally for IRQs; never returns for exceptions.
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16         ; discard int_no and err_code
    iretq
