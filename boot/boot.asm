bits 32
section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00000003
    dd -(0x1BADB002 + 0x00000003)
section .bss
align 4096
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
pd_table:
    resb 16384
align 16
stack_bottom:
    resb 16384
stack_top:
align 16
global tss64
tss64:
    resb 104
multiboot_info:
    resd 1
multiboot_magic:
    resd 1
section .text
global _start
extern kernel
_start:
    cli
    mov esp, stack_top
    mov dword [multiboot_magic], eax
    mov dword [multiboot_info], ebx
    mov eax, pdpt_table
    or eax, 0x07
    mov [pml4_table], eax
    mov eax, pd_table
    or eax, 0x07
    mov [pdpt_table], eax
    add eax, 4096
    mov [pdpt_table + 8], eax
    add eax, 4096
    mov [pdpt_table + 16], eax
    add eax, 4096
    mov [pdpt_table + 24], eax
    mov edi, pd_table
    mov ebx, 0x00000087
    mov ecx, 2048
.map_pd:
    mov dword [edi], ebx
    add ebx, 0x200000
    add edi, 8
    loop .map_pd
    mov eax, pml4_table
    mov cr3, eax
    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, (1 << 31) | (1 << 0)
    mov cr0, eax
    lgdt [gdt64_pointer]
    jmp 0x08:long_mode_start
bits 64
long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, stack_top
    mov rax, stack_top
    mov [tss64 + 4], rax
    mov rax, tss64
    mov word [gdt64_tss + 2], ax
    shr rax, 16
    mov byte [gdt64_tss + 4], al
    mov byte [gdt64_tss + 7], ah
    shr rax, 16
    mov dword [gdt64_tss + 8], eax
    mov ax, 0x28
    ltr ax
    mov edi, dword [multiboot_magic]
    mov esi, dword [multiboot_info]
    call kernel
hang:
    cli
    hlt
    jmp hang
section .rodata
align 8
gdt64:
    dq 0
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
    dq (1<<41) | (1<<44) | (1<<47)
    dq (1<<41) | (1<<44) | (1<<47) | (3<<45)
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) | (3<<45)
gdt64_tss:
    dw 103
    dw 0
    db 0
    db 0x89
    db 0x00
    db 0
    dd 0
    dd 0
gdt64_pointer:
    dw gdt64_pointer - gdt64 - 1
    dq gdt64
