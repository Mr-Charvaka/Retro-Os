[bits 32]

global ap_trampoline_binary
global ap_trampoline_size

section .data
align 4096
ap_trampoline_binary:
    incbin "src/kernel/ap_trampoline.bin"
.end:

ap_trampoline_size:
    dd ap_trampoline_binary.end - ap_trampoline_binary
