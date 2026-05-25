; =================================================================
; ap_trampoline.asm - Secondary Core Entry Point (16-bit to 32-bit)
; =================================================================
; This code is copied to physical address 0x1000 (Vector 0x01).
; It handles the transition from Real Mode to Protected Mode with
; PAE paging enabled, then jumps to the C++ kernel entry point.

[bits 16]
[org 0x1000]

trampoline_entry:
    jmp 0x0000:trampoline_start ; Force CS to 0

; --- Fixed Parameter Block (Offset 0x08) ---
times 8 - ($ - $$) db 0
ap_pdpt_ptr   dd 0x0   ; 0x1008
ap_stack_ptr  dd 0x0   ; 0x100C
ap_entry_ptr  dd 0x0   ; 0x1010
ap_ready_flag dd 0x0   ; 0x1014

trampoline_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x0FF0 ; Safe stack for real mode

    ; Debug: Signal Entry reached ('E')
    ; mov dx, 0x3f8
    ; mov al, 'E'
    ; out dx, al

    ; --- Step 1: Protected Mode Transition ---
    lgdt [gdt_desc_phys]
    
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:prot_mode_32

[bits 32]
prot_mode_32:
    mov ax, 0x10 ; Kernel Data Selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9000 ; Temporary physical stack

    ; Debug: Signal Protected Mode reached ('P')
    ; mov al, 'P'
    ; mov dx, 0x3f8
    ; out dx, al

    ; --- Step 2: Paging (PAE) Enablement ---
    ; 1. Enable PAE
    mov eax, cr4
    or eax, 0x00000020 ; bit 5=PAE
    mov cr4, eax

    ; 2. Load CR3 (PDPT)
    mov eax, [0x1008]
    mov cr3, eax

    ; 3. Enable Paging
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    
    jmp .next
.next:
    nop

    ; Debug: Signal Paging Enabled ('M')
    ; mov al, 'M'
    ; mov dx, 0x3f8
    ; out dx, al

    ; --- Step 3: Transition to Higher-Half ---
    
    ; Trace Marker '1': Before Stack Load
    ; mov al, '1'
    ; mov dx, 0x3f8
    ; out dx, al

    mov esp, [0x100C] ; ap_stack_ptr (Virtual)
    
    ; Trace Marker '2': Before Jump preparation
    ; mov al, '2'
    ; mov dx, 0x3f8
    ; out dx, al

    mov eax, [0x1010] ; ap_entry_ptr (Virtual)

    ; Signal BSP: Assembly Stage Complete (B001)
    mov dword [0x1014], 0xB001

    ; Debug: Signal Final Jump preparation ('J')
    ; mov al, 'J'
    ; mov dx, 0x3f8
    ; out dx, al

    ; 10. CRITICAL HANDSHAKE: Signal arrival to BSP
    ; We set the physical ready flag (0xB002) JUST BEFORE jumping to the higher-half kernel.
    mov dword [0x1014], 0xB002

    ; Use FAR RET to jump to virtual address
    push 0x08         ; Selector
    push eax          ; EIP
    retf 

    ; Trace Marker '3': IF WE FALL THROUGH (error)
    ; mov al, '3'
    ; mov dx, 0x3f8
    ; out dx, al
    hlt

; --- Temporary GDT ---
align 16
gdt_table:
    dq 0x0000000000000000 ; Null
    dq 0x00CF9A000000FFFF ; 0x08: Kernel Code
    dq 0x00CF92000000FFFF ; 0x10: Kernel Data
gdt_end:

gdt_desc_phys:
    dw gdt_end - gdt_table - 1
    dd gdt_table
