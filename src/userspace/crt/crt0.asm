; crt0.asm -- process entry point for Exyde user programs.
;
; At _start, RSP points at the initial stack built by the kernel
; (exec.c::exec_build_initial_stack):
;
;     [rsp + 0]                 argc
;     [rsp + 8]                 argv[0]
;     ...
;     [rsp + 8*argc]            NULL
;     [rsp + 8*(argc + 1)]      envp[0]
;     ...
;     [rsp + 8*(argc+envc+1)]   NULL
;
; SysV AMD64: main(argc, argv, envp) in RDI/RSI/RDX.  We align RSP
; to 16 before 'call', so at main's entry RSP % 16 == 8 (the call
; pushed 8 bytes), as the ABI requires.

BITS 64

section .text
global _start
extern main
extern _exit

_start:
    xor     rbp, rbp                ; terminate frame chain

    mov     rdi, [rsp]              ; argc
    lea     rsi, [rsp + 8]          ; argv
    lea     rax, [rdi + 1]          ; argc + 1
    lea     rdx, [rsi + rax*8]      ; envp = argv + (argc + 1) * 8

    and     rsp, -16
    call    main

    mov     edi, eax                ; main's return value
    call    _exit

.halt:
    hlt
    jmp     .halt
