[bits 32]
global _start
global stack_top
extern main

VIRTUAL_BASE equ 0xC0000000
PDE_INDEX equ (VIRTUAL_BASE >> 22)

; Map 32MB during boot (8 page tables) - Enough for kernel and placement heap
NUM_BOOT_TABLES equ 8
NUM_BOOT_ENTRIES equ (NUM_BOOT_TABLES * 1024)

section .text
_start:
    ; TRACE 1: Entry
    mov al, '1'
    mov dx, 0x3F8
    out dx, al

    ; ── Step 1: Set CR3 ─────────────────────────────────────────────────────
    mov eax, (BootPageDirectory - VIRTUAL_BASE)
    mov cr3, eax
    
    ; ── Step 2: Enable Paging ───────────────────────────────────────────────
    mov eax, cr0
    or eax, 0x80000000 
    mov cr0, eax

    ; TRACE 2: Paging Active
    mov al, '2'
    out dx, al
    
    ; ── Step 3: Enable FPU & SSE (Required for brain_math.cpp) ────────────
    ; Enable FPU
    mov eax, cr0
    and ax, 0xFFFB      ; Clear EM (bit 2) - No emulation
    or ax, 0x2          ; Set MP (bit 1) - Monitor Co-processor
    mov cr0, eax
    fninit              ; Initialize FPU

    ; Enable SSE
    mov eax, cr4
    or ax, (3 << 9)     ; Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov cr4, eax

    ; TRACE 3: SSE Active
    mov al, '3'
    out dx, al

    ; ── Step 4: Jump to Higher Half ─────────────────────────────────────────
    lea eax, [higher_half]
    jmp eax

higher_half:
    ; TRACE 4: In Higher Half
    mov al, '4'
    out dx, al
    mov esp, stack_top
    
    ; Reset EFLAGS
    push 0
    popfd

    call main

    cli
.halt:
    hlt
    jmp .halt

section .data
align 4096
BootPageDirectory:
    ; Map first NUM_BOOT_TABLES for identity mapping
    %assign i 0
    %rep 1024
        %if i < NUM_BOOT_TABLES
            dd (BootPageTables - VIRTUAL_BASE + i * 4096 + 0x003)
        %elif i >= PDE_INDEX && i < (PDE_INDEX + NUM_BOOT_TABLES)
            dd (BootPageTables - VIRTUAL_BASE + (i - PDE_INDEX) * 4096 + 0x003)
        %else
            dd 0
        %endif
        %assign i i+1
    %endrep

align 4096
BootPageTables:
    ; Each table covers 4MB (1024 entries * 4KB)
    ; We pre-fill them to identity map the first 32MB
    %assign i 0
    %rep NUM_BOOT_ENTRIES
        dd (i << 12) + 0x003
        %assign i i+1
    %endrep

section .bss
align 16
stack_bottom:
    resb 65536 ; 64KB stack
stack_top:
