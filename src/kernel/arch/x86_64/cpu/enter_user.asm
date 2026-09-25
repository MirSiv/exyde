; arch_enter_user_mode(vaddr_t entry /rdi/, vaddr_t user_stack_top /rsi/)
;
; iretq frame:
;   SS      = 0x1B (user data selector, RPL=3, GDT index 3)
;   RSP     = user_stack_top
;   RFLAGS  = IF=1, DF=0, TF=0
;   CS      = 0x23 (user code selector, RPL=3, GDT index 4)
;   RIP     = entry
; Never returns.
;
; GS invariant: swapgs is used at every syscall entry/exit to toggle
; MSR_GS_BASE between "0" (user) and "&syscall_state" (kernel).
; That requires MSR_KERNEL_GS_BASE to hold &syscall_state whenever we
; are in ring 3.
;
; "mov gs, ax" would clobber MSR_GS_BASE (loading the segment base
; from the GDT: 0 for flat), but leaves MSR_KERNEL_GS_BASE untouched.
; If we happen to be entered from a syscall context, MSR_KERNEL_GS_BASE
; is 0 at this point (swapgs flipped it), and a fresh user process
; would start with [0, 0] -- its first syscall's swapgs is then a
; no-op and [gs:0] reads whatever lives at physical address 0.
; We therefore force MSR_KERNEL_GS_BASE back to &syscall_state
; explicitly after setting the selector.

BITS 64
section .text
global arch_enter_user_mode
extern gs_kernel_state

%define MSR_KERNEL_GS_BASE 0xC0000102

arch_enter_user_mode:
    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax              ; gs selector = 0x1B, MSR_GS_BASE := 0

    ; MSR_KERNEL_GS_BASE := gs_kernel_state (= &syscall_state)
    mov ecx, MSR_KERNEL_GS_BASE
    mov rax, [rel gs_kernel_state]
    xor edx, edx
    wrmsr

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
