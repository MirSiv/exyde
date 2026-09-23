; arch_enter_user_mode(vaddr_t entry /rdi/, vaddr_t user_stack_top /rsi/)
;
; iretq frame:
;   SS      = 0x1B (user data selector, RPL=3, GDT index 3)
;   RSP     = user_stack_top
;   RFLAGS  = IF=1, DF=0, TF=0
;   CS      = 0x23 (user code selector, RPL=3, GDT index 4)
;   RIP     = entry
; Never returns.

BITS 64
section .text
global arch_enter_user_mode

arch_enter_user_mode:
    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x1B     ; SS
    push rsi            ; RSP
    pushfq
    pop rax
    and rax, ~(1 << 8)  ; clear TF
    and rax, ~(1 << 10) ; clear DF
    or  rax, (1 << 9)   ; set IF
    push rax            ; RFLAGS
    push qword 0x23     ; CS
    push rdi            ; RIP
    iretq
