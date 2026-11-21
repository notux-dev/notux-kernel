bits 64
; Entry point for the `syscall` instruction (installed in IA32_LSTAR by
; msr_syscall_init()).
;
; What the CPU has done when we get here:
;   rcx = user RIP to return to        r11 = user RFLAGS
;   CS/SS = kernel (STAR[47:32]), CPL 0, IF/DF/TF cleared (FMASK)
;   RSP is STILL THE USER STACK -- syscall does not switch stacks.
;
; We therefore park the user rsp in a scratch global (single CPU, IF=0, so
; nothing can interleave), switch to this process's kernel stack (TSS.RSP0,
; kept current by set_kernel_stack()), and build EXACTLY the frame an
; `int 0x80` / timer interrupt would have left: 5-word iret frame, then the
; 15 GPRs. After that the common dispatcher and every blocking trick
; (rewinding RIP by 2, schedule() returning another process's esp) work
; unchanged. We always leave through iretq, never sysretq: sysretq cannot
; resume a frame saved by the timer (it forces rcx/r11 = rip/rflags and
; needs canonical RIP), and iretq works for both.
;
; The syscall instruction is 2 bytes (0F 05), same as int 0x80 (CD 80), so
; the "RIP -= 2 and retry" used by blocking wait4 is valid for both.

global syscall_entry
extern syscall_dispatch
extern tss64

section .bss
align 8
user_rsp_scratch: resq 1

section .text
syscall_entry:
    mov [rel user_rsp_scratch], rsp
    mov rsp, [rel tss64 + 4]        ; RSP0 of the running process
    push qword 0x1B                 ; SS  (user data)
    push qword [rel user_rsp_scratch]
    push r11                        ; RFLAGS (IF set by user code)
    push qword 0x23                 ; CS  (user code)
    push rcx                        ; RIP
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    call syscall_dispatch
    mov rsp, rax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
