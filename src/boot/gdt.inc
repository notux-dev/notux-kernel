gdt64:
    dq 0
.code_seg = $ - gdt64
    dq (1 shl 43) or (1 shl 44) or (1 shl 47) or (1 shl 53)
.data_seg = $ -gdt64
    dq (1 shl 44) or (1 shl 47) or (1 shl 41)
.user_code_seg = $ - gdt64
    dq (1 shl 43) or (1 shl 44) or (1 shl 45) or (1 shl 46) or (1 shl 47) or (1 shl 53)
.user_data_seg = $ - gdt64
    dq (1 shl 41) or (1 shl 44) or (1 shl 45) or (1 shl 46) or (1 shl 47)
.tss_seg = $ - gdt64
    dq 0
    dq 0
.pointer:
    dw $ - gdt64 - 1
    dq gdt64