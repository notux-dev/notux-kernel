format ELF64

public _start
extrn kernel

include 'gdt.inc'

section '.multiboot' align 8

MB2_MAGIC = 0xE85250D6
MB2_ARCH = 0
MB2_HDR_LEN = mb2_header_end - mb2_header
MB2_CHECKSUM = -(MB2_MAGIC + MB2_ARCH + MB2_HDR_LEN)

mb2_header:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_HDR_LEN
    dd MB2_CHECKSUM
    dw 0
    dw 0
    dd 8
mb2_header_end:

section '.bss' writeable align 4096

pml4: rb 4096
pdpt: rb 4096
pd:  rb 4096
stack_bottom: rb 16384
stack_top:

section '.text' executable

use32
_start:
    cli
    mov esp, stack_top

    mov edi, ebx
    mov esi, eax

    push edi
    push esi

    call check_cpuid
    call check_long_mode
    call setup_page_tables
    call enable_paging

    pop esi
    pop edi

    lgdt [gdt64.pointer]

    jmp gdt64.code_seg:long_mode_start

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x200000
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid
    ret
.no_cpuid:
    mov al, 'C'
    jmp error

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 shl 29
    jz .no_long_mode
    ret
.no_long_mode:
    mov al, 'L'
    jmp error

setup_page_tables:
    mov eax, pdpt
    or eax, 11b
    mov [pml4], eax

    mov eax, pd
    or eax, 11b
    mov [pdpt], eax
    mov ecx, 8
    mov eax, 10000011b
    mov edi, pd
.fill_loop:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .fill_loop
    ret

enable_paging:
    mov eax, pml4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 shl 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 shl 8
    wrmsr

    mov eax, cr0
    or eax, 1 shl 31
    mov cr0, eax
    ret

error:
    mov dword [0xB8000], 0x4F524F45
    mov dword [0xB8004], 0x4F3A4F52
    mov byte  [0xB8008], al
    hlt
    jmp $

use64
long_mode_start:
    mov ax, gdt64.data_seg
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack_top

    call kernel

.hang:
    hlt
    jmp .hang

include 'idt.inc'
include 'isr_stubs.inc'