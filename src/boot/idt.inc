extrn isr_common_handler

macro isr_no_err num {
    public isr#num
    isr#num:
        push 0
        push num
        jmp isr_common_stub
}

macro isr_err num {
    public isr#num
    isr#num:
        push num
        jmp isr_common_stub
}

isr_common_stub:
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

    mov rdi, [rsp + 15*8]
    mov rsi, [rsp + 16*8]
    call isr_common_handler

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

    add rsp, 16
    iretq